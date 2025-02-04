/*
 * Copyright 2013-2020 Software Radio Systems Limited
 *
 * This file is part of srsLTE.
 *
 * srsLTE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsLTE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#include <semaphore.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "srslte/phy/ch_estimation/chest_sl.h"
#include "srslte/phy/common/phy_common_sl.h"
#include "srslte/phy/dft/ofdm.h"
#include "srslte/phy/phch/pscch.h"
#include "srslte/phy/phch/pssch.h"
#include "srslte/phy/phch/ra_sl.h"
#include "srslte/phy/phch/sci.h"
#include "srslte/phy/rf/rf.h"
#include "srslte/phy/ue/ue_sync.h"
#include "srslte/phy/utils/bit.h"
#include "srslte/phy/utils/debug.h"
#include "srslte/phy/utils/vector.h"


bool keep_running = true;

srslte_cell_sl_t cell_sl = {.nof_prb = 50, .tm = SRSLTE_SIDELINK_TM4, .cp = SRSLTE_CP_NORM, .N_sl_id = 0};

typedef struct {
  bool     use_standard_lte_rates;
  char*    log_file_name;
  uint32_t file_start_sf_idx;
  uint32_t nof_rx_antennas;
  char*    rf_dev;
  char*    rf_args;
  double   rf_freq;
  float    rf_gain;

  // Sidelink specific args
  uint32_t size_sub_channel;
  uint32_t num_sub_channel;
} prog_args_t;

void args_default(prog_args_t* args)
{
  args->use_standard_lte_rates = false;
  args->log_file_name          = NULL;
  args->file_start_sf_idx      = 0;
  args->nof_rx_antennas        = 1;
  args->rf_dev                 = "";
  args->rf_args                = "";
  args->rf_freq                = 5.92e9;
  args->rf_gain                = 50;
  args->size_sub_channel       = 10;
  args->num_sub_channel        = 5;
}

static srslte_rf_t radio;
static prog_args_t prog_args;

void sig_int_handler(int signo)
{
  printf("SIGINT received. Exiting...\n");
  if (signo == SIGINT) {
    keep_running = false;
  } else if (signo == SIGSEGV) {
    exit(1);
  }
}

void usage(prog_args_t* args, char* prog)
{
  printf("Usage: %s [aAcdgmnoprstv] -f rx_frequency_hz\n", prog);
  printf("\t-a RF args [Default %s]\n", args->rf_args);
  printf("\t-A nof_rx_antennas [Default %d]\n", args->nof_rx_antennas);
  printf("\t-c N_sl_id [Default %d]\n", cell_sl.N_sl_id);
  printf("\t-d RF devicename [Default %s]\n", args->rf_dev);
  printf("\t-g RF Gain [Default %.2f dB]\n", args->rf_gain);
  printf("\t-m Start subframe_idx [Default %d]\n", args->file_start_sf_idx);
  printf("\t-n num_sub_channel [Default for 50 prbs %d]\n", args->num_sub_channel);
  printf("\t-o log_file_name.\n");
  printf("\t-p nof_prb [Default %d]\n", cell_sl.nof_prb);
  printf("\t-r use_standard_lte_rates [Default %i]\n", args->use_standard_lte_rates);
  printf("\t-s size_sub_channel [Default for 50 prbs %d]\n", args->size_sub_channel);
  printf("\t-t Sidelink transmission mode {1,2,3,4} [Default %d]\n", (cell_sl.tm + 1));
  printf("\t-v srslte_verbose\n");

}

void parse_args(prog_args_t* args, int argc, char** argv)
{
  // printf("RUNNING THIS***********************************************************\n");
  int opt;
  args_default(args);

  while ((opt = getopt(argc, argv, "aAcdfgmnoprsv")) != -1) {
    switch (opt) {
      case 'a':
        args->rf_args = argv[optind];
        break;
      case 'A':
        args->nof_rx_antennas = (int32_t)strtol(argv[optind], NULL, 10);
        break;
      case 'c':
        cell_sl.N_sl_id = (int32_t)strtol(argv[optind], NULL, 10);
        break;
      case 'd':
        args->rf_dev = argv[optind];
        break;
      case 'f':
        args->rf_freq = strtof(argv[optind], NULL);
        break;
      case 'g':
        args->rf_gain = strtof(argv[optind], NULL);
        break;
      case 'm':
        args->file_start_sf_idx = (uint32_t)strtol(argv[optind], NULL, 10);
        break;
      case 'n':
        // printf("n is %d\n", (int32_t)strtol(argv[optind], NULL, 10));
        args->num_sub_channel = (int32_t)strtol(argv[optind], NULL, 10);
        break;
      case 'o':
        args->log_file_name = argv[optind];
        break;
      case 'p':
        cell_sl.nof_prb = (int32_t)strtol(argv[optind], NULL, 10);
        break;
      case 'r':
        args->use_standard_lte_rates = true;
        break;
      case 's':
        // printf("s is %d\n", (int32_t)strtol(argv[optind], NULL, 10));
        args->size_sub_channel = (int32_t)strtol(argv[optind], NULL, 10);
        break;
      case 'v':
        srslte_verbose++;
        break;
      default:
        usage(args, argv[0]);
        exit(-1);
    }
  }
  if (args->rf_freq < 0) {
    usage(args, argv[0]);
    exit(-1);
  }
}

#ifndef DISABLE_RF
int srslte_rf_recv_wrapper(void* h, cf_t* data[SRSLTE_MAX_PORTS], uint32_t nsamples, srslte_timestamp_t* t)
{
  DEBUG(" ----  Receive %d samples  ---- \n", nsamples);
  void* ptr[SRSLTE_MAX_PORTS];
  for (int i = 0; i < SRSLTE_MAX_PORTS; i++) {
    ptr[i] = data[i];
  }
  return srslte_rf_recv_with_time_multi(h, ptr, nsamples, true, &t->full_secs, &t->frac_secs);
}
#endif // DISABLE_RF

int main(int argc, char** argv)
{
  signal(SIGINT, sig_int_handler);
  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, SIGINT);
  sigprocmask(SIG_UNBLOCK, &sigset, NULL);

  uint32_t num_decoded_sci = 0;
  uint32_t num_decoded_tb  = 0;

  parse_args(&prog_args, argc, argv);

  /***** logfile *******/
  struct tm* timeinfo;
  time_t     current_time = time(0); // Get the system time
  timeinfo                = localtime(&current_time);

  FILE *logfile;
  if (prog_args.log_file_name) {
    logfile = fopen(prog_args.log_file_name, "w");
  } else {

    const char * home = getenv("HOME");
    const char * subPath = "/v2x_pssch_ue_logfiles";
    char * path = (char*)malloc(strlen(home) + strlen(subPath) + 1);
    strcpy(path, home);
    strcat(path, subPath);

    mkdir(path, 0700);
    char filename[100];

    sprintf(filename,
            "%s/v2x_pssch_ue_%d_%d_%d-%d_%d_%d.csv",
            path,
            timeinfo->tm_year + 1900,
            timeinfo->tm_mon + 1,
            timeinfo->tm_mday,
            timeinfo->tm_hour,
            timeinfo->tm_min,
            timeinfo->tm_sec);

    logfile = fopen(filename, "w");

    free(path);
  }

  // write header
  // fprintf(logfile, "rx_timestamp_us,prb_start_idx,nof_prb,N_x_id,mcs_idx,rv_idx,sf_idx\n");

  /***** Init *******/
  srslte_use_standard_symbol_size(prog_args.use_standard_lte_rates);

  srslte_sl_comm_resource_pool_t sl_comm_resource_pool;
  if (srslte_sl_comm_resource_pool_get_default_config(&sl_comm_resource_pool, cell_sl) != SRSLTE_SUCCESS) {
    ERROR("Error initializing sl_comm_resource_pool\n");
    return SRSLTE_ERROR;
  }


  printf("Opening RF device...\n");

  if (srslte_rf_open_multi(&radio, prog_args.rf_args, prog_args.nof_rx_antennas)) {
    ERROR("Error opening rf\n");
    exit(-1);
  }

  printf("Set RX freq: %.6f MHz\n",
         srslte_rf_set_rx_freq(&radio, prog_args.nof_rx_antennas, prog_args.rf_freq) / 1e6);
  printf("Set RX gain: %.1f dB\n", srslte_rf_set_rx_gain(&radio, prog_args.rf_gain));
  int srate = srslte_sampling_freq_hz(cell_sl.nof_prb);

  if (srate != -1) {
    printf("Setting sampling rate %.2f MHz\n", (float)srate / 1000000);
    float srate_rf = srslte_rf_set_rx_srate(&radio, (double)srate);
    if (srate_rf != srate) {
      ERROR("Could not set sampling rate\n");
      exit(-1);
    }
  } else {
    ERROR("Invalid number of PRB %d\n", cell_sl.nof_prb);
    exit(-1);
  }

  // allocate Rx buffers for 1ms worth of samples
  uint32_t sf_len = SRSLTE_SF_LEN_PRB(cell_sl.nof_prb);
  printf("Using a SF len of %d samples\n", sf_len);

  cf_t* rx_buffer[SRSLTE_MAX_CHANNELS] = {};     //< For radio to receive samples
  cf_t* sf_buffer[SRSLTE_MAX_PORTS]    = {NULL}; ///< For OFDM object to store subframe after FFT

  for (int i = 0; i < prog_args.nof_rx_antennas; i++) {
    rx_buffer[i] = srslte_vec_cf_malloc(sf_len);
    if (!rx_buffer[i]) {
      perror("malloc");
      exit(-1);
    }
    sf_buffer[i] = srslte_vec_cf_malloc(sf_len);
    if (!sf_buffer[i]) {
      perror("malloc");
      exit(-1);
    }
  }

  uint32_t sf_n_re             = SRSLTE_CP_NSYMB(SRSLTE_CP_NORM) * SRSLTE_NRE * 2 * cell_sl.nof_prb;
  cf_t*    equalized_sf_buffer = srslte_vec_malloc(sizeof(cf_t) * sf_n_re);

  // RX
  srslte_ofdm_t     fft[SRSLTE_MAX_PORTS];
  srslte_ofdm_cfg_t ofdm_cfg = {};
  ofdm_cfg.nof_prb           = cell_sl.nof_prb;
  ofdm_cfg.cp                = SRSLTE_CP_NORM;
  ofdm_cfg.rx_window_offset  = 0.0f;
  ofdm_cfg.normalize         = true;
  ofdm_cfg.sf_type           = SRSLTE_SF_NORM;
  ofdm_cfg.freq_shift_f      = -0.5;
  for (int i = 0; i < prog_args.nof_rx_antennas; i++) {
    ofdm_cfg.in_buffer  = rx_buffer[0];
    ofdm_cfg.out_buffer = sf_buffer[0];

    if (srslte_ofdm_rx_init_cfg(&fft[i], &ofdm_cfg)) {
      ERROR("Error initiating FFT\n");
      exit(-1);
    }
  }

  // SCI
  srslte_sci_t sci;
  srslte_sci_init(&sci, cell_sl, sl_comm_resource_pool);
  uint8_t sci_rx[SRSLTE_SCI_MAX_LEN]      = {};
  char    sci_msg[SRSLTE_SCI_MSG_MAX_LEN] = {};

  // init PSCCH object
  static srslte_pscch_t pscch = {};
  if (srslte_pscch_init(&pscch, SRSLTE_MAX_PRB) != SRSLTE_SUCCESS) {
    ERROR("Error in PSCCH init\n");
    return SRSLTE_ERROR;
  }

  if (srslte_pscch_set_cell(&pscch, cell_sl) != SRSLTE_SUCCESS) {
    ERROR("Error in PSCCH set cell\n");
    return SRSLTE_ERROR;
  }

  // PSCCH Channel estimation
  srslte_chest_sl_cfg_t pscch_chest_sl_cfg = {};
  srslte_chest_sl_t     pscch_chest        = {};
  if (srslte_chest_sl_init(&pscch_chest, SRSLTE_SIDELINK_PSCCH, cell_sl, sl_comm_resource_pool) != SRSLTE_SUCCESS) {
    ERROR("Error in chest PSCCH init\n");
    return SRSLTE_ERROR;
  }

  // init PSSCH object
  static srslte_pssch_t pssch = {};
  if (srslte_pssch_init(&pssch, cell_sl, sl_comm_resource_pool) != SRSLTE_SUCCESS) {
    ERROR("Error initializing PSSCH\n");
    return SRSLTE_ERROR;
  }

  srslte_chest_sl_cfg_t pssch_chest_sl_cfg = {};
  srslte_chest_sl_t     pssch_chest        = {};
  if (srslte_chest_sl_init(&pssch_chest, SRSLTE_SIDELINK_PSSCH, cell_sl, sl_comm_resource_pool) != SRSLTE_SUCCESS) {
    ERROR("Error in chest PSSCH init\n");
    return SRSLTE_ERROR;
  }

  uint8_t tb[SRSLTE_SL_SCH_MAX_TB_LEN] = {};

  srslte_ue_sync_t ue_sync = {};
  srslte_cell_t cell = {};
  cell.nof_prb       = cell_sl.nof_prb;
  cell.cp            = SRSLTE_CP_NORM;
  cell.nof_ports     = 1;

  if (srslte_ue_sync_init_multi_decim_mode(&ue_sync,
                                           cell.nof_prb,
                                           false,
                                           srslte_rf_recv_wrapper,
                                           prog_args.nof_rx_antennas,
                                           (void*)&radio,
                                           1,
                                           SYNC_MODE_GNSS)) {
    fprintf(stderr, "Error initiating sync_gnss\n");
    exit(-1);
  }

  if (srslte_ue_sync_set_cell(&ue_sync, cell)) {
    ERROR("Error initiating ue_sync\n");
    exit(-1);
  }

  srslte_rf_start_rx_stream(&radio, false);

  uint32_t subframe_count      = 0;
  uint32_t pscch_prb_start_idx = 0;

  uint32_t current_sf_idx = 0;
 
  fprintf(logfile, "subframe, timestamp (ms), resource_blocks\n");   // ADDED

  while (keep_running) {

    // receive subframe from radio
    int ret = srslte_ue_sync_zerocopy(&ue_sync, rx_buffer, sf_len);
    if (ret < 0) {
      ERROR("Error calling srslte_ue_sync_work()\n");
    }

    // update SF index
    current_sf_idx = srslte_ue_sync_get_sfidx(&ue_sync);

    // do FFT (on first port)
    srslte_ofdm_rx_sf(&fft[0]);


    /* 
    * START OF ADDED CODE
    */ 
    // uint32_t total_subchannels = sl_comm_resource_pool.num_sub_channel; 
    // bool resource_block[total_subchannels]; // initialize with the size of the total number of subchannels
    uint32_t nsch = sl_comm_resource_pool.num_sub_channel;  // number of subchannels
    uint32_t ssch = sl_comm_resource_pool.size_sub_channel;  // number of size of subchannels in resource blocks
    uint32_t num_resource_blocks = nsch * ssch; 
    bool resource_block[num_resource_blocks]; // initialize with the size of the total number of subchannels
    /* END OF ADDED CODE
    */ 
    for (int sub_channel_idx = 0; sub_channel_idx < sl_comm_resource_pool.num_sub_channel; sub_channel_idx++) {
      pscch_prb_start_idx = sub_channel_idx * sl_comm_resource_pool.size_sub_channel;

      for (uint32_t cyclic_shift = 0; cyclic_shift <= 9; cyclic_shift += 3) {

        // PSCCH Channel estimation
        pscch_chest_sl_cfg.cyclic_shift  = cyclic_shift;
        pscch_chest_sl_cfg.prb_start_idx = pscch_prb_start_idx;
        srslte_chest_sl_set_cfg(&pscch_chest, pscch_chest_sl_cfg);
        srslte_chest_sl_ls_estimate_equalize(&pscch_chest, sf_buffer[0], equalized_sf_buffer);

        if (srslte_pscch_decode(&pscch, equalized_sf_buffer, sci_rx, pscch_prb_start_idx) == SRSLTE_SUCCESS) {
          printf("pscch decoded!\n");
          if (srslte_sci_format1_unpack(&sci, sci_rx) == SRSLTE_SUCCESS) {   // WE CARE ABOUT THIS !!!
                                                    // unpacked SCI format 1 data is in sci variable (refer to Fabian Eckermann's paper for more details)
            srslte_sci_info(&sci, sci_msg, sizeof(sci_msg));
            /*
            * START OF ADDED CODE TO PROBE SCI FOR SCI FORMAT 1 INFORMATION. Mainly want to extract resource reservation and RIV info
            */ 
            // get the time stamp 
            // uint32_t rsc_reserv = sci.resource_reserv;  // resource reservation field; 4 bits
            // uint32_t riv = sci.riv;   // riv "resource indication value"; 0-8 bits
            /*
            * END OF ADDED CODE TO PROBE SCI FOR SCI FORMAT 1 INFORMATION.
            */ 

            fprintf(stdout, "%s", sci_msg);

            num_decoded_sci++;

            // Decode PSSCH
            uint32_t sub_channel_start_idx = 0;
            uint32_t L_subCH               = 0;
            srslte_ra_sl_type0_from_riv(
                sci.riv, sl_comm_resource_pool.num_sub_channel, &L_subCH, &sub_channel_start_idx);

            /*
            * START OF ADDED CODE TO PROBE SCI FOR SCI FORMAT 1 INFORMATION. Mainly want to extract resource reservation and RIV info
            */ 
            // sub_channel_start_idx = starting sub-channel index 
            // L_subCH = the length in terms of contiguously allocated sub-channels
            // Assumption1: using adjacent channelization scheme
            // Assumption2: the length of L_subCH includes SCI allcation 
            // Assumption3: the sub-channel indexes begin at 1 and go up to (and including) total_subchannels (need to provide reasoning so)
            // *TODO*: need to figure out what C-V2X channelization scheme is used (i.e. adjacent or non-adjacent)
            // *TODO*: need to cover edge cases, e.g. when L_subCH=0 ?? 
            // uint32_t sub_channel_end_idx = sub_channel_start_idx + L_subCH - 1; // define the end of the channel utilization 
            uint32_t rb_start_idx = (sub_channel_start_idx - 1) * ssch + 1; // define the start of the contiguous resource blocks allocation 
            uint32_t rb_end_idx = rb_start_idx + L_subCH * ssch - 1; // define the end of the contiguous resource blocks allocation 
            // printf("Num resource blocks is %u\n", num_resource_blocks);
            printf("sub_channel_start_idx=%u\n", sub_channel_start_idx);
            for (int rb = 0; rb < num_resource_blocks; rb++) {  // rb = "resource block"
                if ((rb+1) < rb_start_idx || (rb+1) > rb_end_idx)
                    resource_block[rb] = false; // RB not allocated
                else 
                    resource_block[rb] = true;  // RB is allocated
            }

            // print format: <time_stamp> <subchannel_utilization>
            printf("Time Stamp: %lu ms\t", (uint64_t) round(srslte_timestamp_real(&ue_sync.last_timestamp) * 1e3)); // get time stamp in milliseconds
            printf("Resource Blocks: ");
            for (int i = 0; i < num_resource_blocks; i++) {
                printf("%d", resource_block[i]);
            }
            // printf(" num side channels: %d\t size of size channels: %d", nsch, ssch);
            printf("\n"); 

            fprintf(logfile, "%u\t", current_sf_idx); 
            fprintf(logfile, "%lu\t", (uint64_t) round(srslte_timestamp_real(&ue_sync.last_timestamp) * 1e3)); 
            for (int i = 0; i < num_resource_blocks; i++) {
                fprintf(logfile, "%d", resource_block[i]);
            }
            fprintf(logfile, "\n");

            /*
            * END OF ADDED CODE TO PROBE SCI FOR SCI FORMAT 1 INFORMATION.
            */ 

            // 3GPP TS 36.213 Section 14.1.1.4C
            uint32_t pssch_prb_start_idx = (sub_channel_idx * sl_comm_resource_pool.size_sub_channel) +
                                           pscch.pscch_nof_prb + sl_comm_resource_pool.start_prb_sub_channel;
            uint32_t nof_prb_pssch = ((L_subCH + sub_channel_idx) * sl_comm_resource_pool.size_sub_channel) -
                                     pssch_prb_start_idx + sl_comm_resource_pool.start_prb_sub_channel;

            // make sure PRBs are valid for DFT precoding
            nof_prb_pssch = srslte_dft_precoding_get_valid_prb(nof_prb_pssch);

            uint32_t N_x_id = 0;
            for (int j = 0; j < SRSLTE_SCI_CRC_LEN; j++) {
              N_x_id += pscch.sci_crc[j] * exp2(SRSLTE_SCI_CRC_LEN - 1 - j);
            }

            uint32_t rv_idx = 0;
            if (sci.retransmission == true) {
              rv_idx = 1;
            }

            // PSSCH Channel estimation
            pssch_chest_sl_cfg.N_x_id        = N_x_id;
            pssch_chest_sl_cfg.sf_idx        = current_sf_idx;
            pssch_chest_sl_cfg.prb_start_idx = pssch_prb_start_idx;
            pssch_chest_sl_cfg.nof_prb       = nof_prb_pssch;
            srslte_chest_sl_set_cfg(&pssch_chest, pssch_chest_sl_cfg);
            srslte_chest_sl_ls_estimate_equalize(&pssch_chest, sf_buffer[0], equalized_sf_buffer);

            srslte_pssch_cfg_t pssch_cfg = {
                pssch_prb_start_idx, nof_prb_pssch, N_x_id, sci.mcs_idx, rv_idx, current_sf_idx};
            if (srslte_pssch_set_cfg(&pssch, pssch_cfg) == SRSLTE_SUCCESS) {
              if (srslte_pssch_decode(&pssch, equalized_sf_buffer, tb, SRSLTE_SL_SCH_MAX_TB_LEN) == SRSLTE_SUCCESS) {
                num_decoded_tb++;

                printf("\n\n\n\n\n*******BAHHH HUMBUG***********\n\n\n\n\n"); // ADDED

                // ADDED -- COMMENTED THIS WHOLE FPRINTF PART OUT
                // write logfile
                // fprintf(logfile,"%lu,%d,%d,%d,%d,%d,%d\n",
                //         (uint64_t) round(srslte_timestamp_real(&ue_sync.last_timestamp) * 1e6),
                //         pssch_prb_start_idx,
                //         nof_prb_pssch,
                //         N_x_id,
                //         sci.mcs_idx,
                //         rv_idx,
                //         current_sf_idx);


              }
            }
          }
        }
        if (SRSLTE_VERBOSE_ISDEBUG()) {
          char filename[64];
          snprintf(filename,
                   64,
                   "pscch_rx_syms_sf%d_shift%d_prbidx%d.bin",
                   subframe_count,
                   cyclic_shift,
                   pscch_prb_start_idx);
          printf("Saving PSCCH symbols (%d) to %s\n", pscch.E / SRSLTE_PSCCH_QM, filename);
          srslte_vec_save_file(filename, pscch.mod_symbols, pscch.E / SRSLTE_PSCCH_QM * sizeof(cf_t));
        }
      }
    }

    // printf("Current sf idx is %u\n", current_sf_idx); // ADDED
    current_sf_idx = (current_sf_idx + 1) % 10;

    subframe_count++;
  }

  fclose(logfile);
  printf("num_decoded_sci=%d num_decoded_tb=%d\n", num_decoded_sci, num_decoded_tb);

  srslte_rf_stop_rx_stream(&radio);
  srslte_rf_close(&radio);
  srslte_ue_sync_free(&ue_sync);
  srslte_sci_free(&sci);
  srslte_pscch_free(&pscch);
  srslte_chest_sl_free(&pscch_chest);
  srslte_chest_sl_free(&pssch_chest);

  for (int i = 0; i < prog_args.nof_rx_antennas; i++) {
    if (rx_buffer[i]) {
      free(rx_buffer[i]);
    }
    if (sf_buffer[i]) {
      free(sf_buffer[i]);
    }
    srslte_ofdm_rx_free(&fft[i]);
  }

  if (equalized_sf_buffer) {
    free(equalized_sf_buffer);
  }

  return SRSLTE_SUCCESS;
}

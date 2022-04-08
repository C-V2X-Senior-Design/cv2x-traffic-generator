# Changelog for debugging cv2x transmit and pssch_ue (in cv2x traffic generator fork)
### Author: Julia Zeng (zjulia@bu.edu)


##### Issue No.1
No packets were passing the srslte_pssch_decode in pssch_ue.c, i.e.
num_decoded_tb always equaled 0, and therefore no PCAPs were generated.


** Problemtatic filename:function:linenum? **
- /lib/src/phy/phch/pssch.c:srslte_pssch_decode:464
- /lib/src/phy/phch/pssch.c:srslte_pssch_decode:487

** What is the issue in that line? **
- It is not passing the checksum, i.e. srslte_bit_diff(), which essentially checks that every bit is the same and as a result an error is returned to pssch_ue indicating the packet cannot be decoded. 

** How to recreate the error? **
- Add ERROR("Checksum error") error printing statements to those two places
- Run pssch_ue.c. You should now see those "Checksum error" error messages printed to the console

** How I "solved" this issue? **
- Commented out CRC in /lib/src/phy/phch/pssch.c:srslte_pssch_decode:464 and /lib/src/phy/phch/pssch.c:srslte_pssch_decode:487
- Now, pssch_ue will generate PCAPs in tmp/pssch.pcap, but this is garbage



##### Issue No.2
This is not an issue, but instructions on how to populate the transport block (payload) in C-V2X traffic generator.

- In the original fork, C-V2X does not populate the tb[] array, possibly because it is only used for stress-testing of SCI. 
- Edit tb to populate the array with up to SRSLTE_SL_SCH_MAX_TB_LEN bits. 
- e.g.: cv2x_traffic_generator.c:426 populates tb with all 1s. 



##### Issue No.3
(Cross-documented with modSrsRan)
This is not an issue, but rather an explanation for why we do not need to
account for the non-adjacent C-V2X subchannelization scheme, just the adjacent scheme.
Refer to Fabian Eckermann's paper *SDR-based Open-Source C-V2X Traffic Generator for Stress 
Testing Vehicular Communication* for explanation of C-V2X subchannelization schemes. 

- cv2x-traffic-generator-fork/lib/src/phy/common/phy_common_sl.c:407 defines ```* @param adjacency_pscch_pssch subchannelization adjacency ```
- cv2x_traffic_generator.c:372 calls srslte_sl_comm_resource_pool_set_cfg() which is hardcoded to set adjacency_pscch_pssch=true.
- Why I think true=adjacent and false=non-adjacent: cv2x-traffic-generator-fork/lib/src/phy/ue/ue_sl.c:658
  ``` 
  if (q->sl_comm_resource_pool.adjacency_pscch_pssch) {
	pscch_prb_start_idx = sub_channel_idx * q->sl_comm_resource_pool.size_sub_channel; 
  } else {
	pscch_prb_start_idx = sub_channel_idx * 2;
  }
  ```
  If adjacency_pscch_pssch=true, then pssch starting block will be a multiple of the subchannel size, 
  else it is a multiple of 2. 

- modSrsRAN/lib/src/phy/common/phy_common_sl.c:srsran_sl_comm_resource_pool_get_default_config:346, which is called in pssch_ue, hardcodes adjacency_pscch_pssch=true.
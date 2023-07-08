/*
 *
 * Copyright (C) 2013 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _BT_ATHOME_SPLITTER_PRIV_H_
#define _BT_ATHOME_SPLITTER_PRIV_H_

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci.h>

/*
 * ACL packets on LE transport have a maximum payload of 27 bytes. However, the
 * BTLE chip we use on the remote (for now) has issues sending 27-byte packets
 * with encryption quickly at 7.5 connection interval. This issue goes away if
 * 26-byte packets are used. Therefore for our protocol all packets are limited
 * to 26 bytes.
 */
#define ACL_LE_PKT_SZ					27
#define ACL_AAH_PKT_SZ					26

#define ACL_CONN_MASK					0x0FFF


#define HCI_ERR_Success					0x00
#define HCI_ERR_Unknown_HCI_Command			0x01

#define HCI_OGF_Vendor					0x3F
# define HCI_CMD_Marvell_Set_MCA			0x71
# define HCI_CMD_Marvell_BLE_COEX_MODE_CONFIG		0x8A
# define HCI_CMD_Marvell_BLE_Set_Extra_Spacing		0x8F
/* params: BT_MCA, LE_MCA */
#  define HCI_Marvell_MCA_500_ppm			0x00
#  define HCI_Marvell_MCA_250_ppm			0x01
#  define HCI_Marvell_MCA_150_ppm			0x02
#  define HCI_Marvell_MCA_100_ppm			0x03
#  define HCI_Marvell_MCA_75_ppm			0x04
#  define HCI_Marvell_MCA_50_ppm			0x05
#  define HCI_Marvell_MCA_30_ppm			0x06
#  define HCI_Marvell_MCA_20_ppm			0x07



#define HCI_OGF_Link_Control				1
# define HCI_CMD_Disconnect				0x0006

#define HCI_OGF_Controller_And_Baseband			0x03
# define HCI_Set_Event_Mask				0x0001
# define HCI_Reset					0x0003
# define HCI_Read_LE_Host_Support			0x006C
# define HCI_Write_LE_Host_Support			0x006D

#define HCI_OGF_Informational				0x04
# define HCI_Read_Local_Version_Information		0x0001
# define HCI_Read_Local_Supported_Commands		0x0002
# define HCI_Read_Local_Supported_Features		0x0003
# define HCI_Read_Local_Supported_Extended_Features	0x0004
# define HCI_Read_Buffer_Size				0x0005
# define HCI_Read_Data_Block_Size			0x000A

#define HCI_OGF_LE					0x08
# define HCI_LE_Set_Event_Mask				0x0001
# define HCI_LE_Read_Buffer_Size			0x0002
# define HCI_LE_Read_Local_Supported_Features		0x0003
# define HCI_LE_Set_Random_Address			0x0005
# define HCI_LE_Set_Advertising_Parameters		0x0006
# define HCI_LE_Read_Advertising_Channel_Tx_Power	0x0007
# define HCI_LE_Set_Advertising_Data			0x0008
# define HCI_LE_Set_Scan_Response_Data			0x0009
# define HCI_LE_Set_Advertise_Enable			0x000A
# define HCI_LE_Set_Scan_Parameters			0x000B
# define HCI_LE_Set_Scan_Enable				0x000C
# define HCI_LE_Create_Connection			0x000D
# define HCI_LE_Create_Connection_Cancel		0x000E
# define HCI_LE_Read_White_List_Size			0x000F
# define HCI_LE_Clear_White_List			0x0010
# define HCI_LE_Add_Device_To_White_List		0x0011
# define HCI_LE_Remove_Device_From_White_List		0x0012
# define HCI_LE_Connection_Update			0x0013
# define HCI_LE_Set_Host_Channel_Classification		0x0014
# define HCI_LE_Read_Channel_Map			0x0015
# define HCI_LE_Read_Remote_Used_Features		0x0016
# define HCI_LE_Encrypt					0x0017
# define HCI_LE_Rand					0x0018
# define HCI_LE_Start_Encryption			0x0019
# define HCI_LE_Long_Term_Key_Request_Reply		0x001A
# define HCI_LE_Long_Term_Key_Requested_Negative_Reply	0x001B
# define HCI_LE_Read_Supported_States			0x001C
# define HCI_LE_Receiver_Test				0x001D
# define HCI_LE_Transmitter_Test			0x001E
# define HCI_LE_Test_End 				0x001F

#define SCAN_EVT_ADV_IND				0
#define SCAN_EVT_ADV_DIRECT_IND				1
#define SCAN_EVT_ADV_SCAN_IND				2
#define SCAN_EVT_ADV_NONCONN_IND			3
#define SCAN_EVT_SCAN_RSP				4


#define HCI_LE_ADV_DATA_MAX_LEN				31

#define HCI_EV_ENCR_REFRESH		0x30
struct hci_ev_encrypt_refresh {
	__u8     status;
	__le16   handle;
} __packed;

#define HCI_EV_LE_CONN_UPDATE		0x03
struct hci_ev_le_conn_update {
	__u8     status;
	__le16   handle;
	__le16   interval;
	__le16   latency;
	__le16   supervision_timeout;
} __packed;

#define HCI_EV_LE_READ_REMOTE_FEATURES_COMP	0x04
struct hci_ev_le_read_remote_features {
	__u8     status;
	__le16   handle;
	__u8     feature_mask[8];
} __packed;

struct hci_cp_le_encrypt_req {
	__u8     key[16];
	__u8     in[16];
} __packed;

struct hci_ev_le_encrypt_resp {
	__u8     status;
	__u8     out[16];
} __packed;

struct hci_ev_le_rand_resp {
	__u8    status;
	__u8    bytes[8];
} __packed;

#define BT_HCI_VERSION_3	5
#define BT_HCI_VERSION_4	6

#define BT_LMP_VERSION_3	5
#define BT_LMP_VERSION_4	6

struct read_le_host_supported_reply {
	__u8 status;
	__u8 le;
	__u8 simul;
} __packed;

struct read_local_version_reply {
	__u8     status;
	__u8     hci_version;
	__u16    revision;
	__u8     lmp_version;
	__u16    manuf;
	__u16    lmp_subversion;
} __packed;

struct read_support_cmds_reply {
	__u8     status;
	__u8     commands[64];
} __packed;

struct read_support_ftrs_reply {
	__u8     status;
	__u8     features[8];
} __packed;

struct read_support_ext_ftrs_reply {
	__u8     status;
	__u8     page_nr;
	__u8     max_page_nr;
	__u8     features[8];
} __packed;

struct comp_pkts_info {
	__le16   handle;
	__le16   count;
} __packed;

struct num_comp_pkts {
	__u8     num_hndl;
	struct comp_pkts_info handles[0];
} __packed;

#endif

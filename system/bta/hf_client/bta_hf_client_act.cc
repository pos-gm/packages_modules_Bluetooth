/******************************************************************************
 *
 *  Copyright (c) 2014 The Android Open Source Project
 *  Copyright (C) 2003-2012 Broadcom Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at:
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************/

/******************************************************************************
 *
 *  This file contains action functions for the handsfree client.
 *
 ******************************************************************************/

#include <string.h>
#include "bt_utils.h"
#include "bta_api.h"
#include "bta_dm_api.h"
#include "bta_hf_client_api.h"
#include "bta_hf_client_int.h"
#include "bta_sys.h"
#include "l2c_api.h"
#include "osi/include/compat.h"
#include "osi/include/osi.h"
#include "port_api.h"
#include "utl.h"

/*****************************************************************************
 *  Constants
 ****************************************************************************/

/* maximum length of data to read from RFCOMM */
#define BTA_HF_CLIENT_RFC_READ_MAX 512

/*******************************************************************************
 *
 * Function         bta_hf_client_start_close
 *
 * Description      Start the process of closing SCO and RFCOMM connection.
 *
 *
 * Returns          void
 *
 ******************************************************************************/
void bta_hf_client_start_close(tBTA_HF_CLIENT_DATA* p_data) {
  tBTA_HF_CLIENT_CB* client_cb =
      bta_hf_client_find_cb_by_handle(p_data->hdr.layer_specific);
  if (client_cb == NULL) {
    APPL_TRACE_ERROR("%s: wrong handle to control block %d", __func__,
                     p_data->hdr.layer_specific);
    return;
  }

  /* Take the link out of sniff and set L2C idle time to 0 */
  bta_dm_pm_active(client_cb->scb.peer_addr);
  L2CA_SetIdleTimeoutByBdAddr(client_cb->scb.peer_addr, 0, BT_TRANSPORT_BR_EDR);

  /* if SCO is open close SCO and wait on RFCOMM close */
  if (client_cb->scb.sco_state == BTA_HF_CLIENT_SCO_OPEN_ST) {
    client_cb->scb.sco_close_rfc = true;
  } else {
    bta_hf_client_rfc_do_close(p_data);
  }

  /* always do SCO shutdown to handle all SCO corner cases */
  bta_hf_client_sco_shutdown(client_cb);
}

/*******************************************************************************
 *
 * Function         bta_hf_client_start_open
 *
 * Description      This starts an HF Client open.
 *
 *
 * Returns          void
 *
 ******************************************************************************/
void bta_hf_client_start_open(tBTA_HF_CLIENT_DATA* p_data) {
  tBTA_HF_CLIENT_CB* client_cb =
      bta_hf_client_find_cb_by_handle(p_data->hdr.layer_specific);
  if (client_cb == NULL) {
    APPL_TRACE_ERROR("%s: wrong handle to control block %d", __func__,
                     p_data->hdr.layer_specific);
    return;
  }

  /* store parameters */
  if (p_data) {
    bdcpy(client_cb->scb.peer_addr, p_data->api_open.bd_addr);
    client_cb->scb.cli_sec_mask = p_data->api_open.sec_mask;
  }

  /* Check if RFCOMM has any incoming connection to avoid collision. */
  BD_ADDR pending_bd_addr;
  if (PORT_IsOpening(pending_bd_addr)) {
    /* Let the incoming connection goes through.                        */
    /* Issue collision for now.                                         */
    /* We will decide what to do when we find incoming connection later.*/
    bta_hf_client_collision_cback(0, BTA_ID_HS, 0, client_cb->scb.peer_addr);
    return;
  }

  /* close server */
  bta_hf_client_close_server(client_cb);

  /* set role */
  client_cb->scb.role = BTA_HF_CLIENT_INT;

  /* do service search */
  bta_hf_client_do_disc(client_cb);
}

/*******************************************************************************
 *
 * Function         bta_hf_client_cback_open
 *
 * Description      Send open callback event to application.
 *
 *
 * Returns          void
 *
 ******************************************************************************/
static void bta_hf_client_cback_open(tBTA_HF_CLIENT_CB* client_cb,
                                     tBTA_HF_CLIENT_STATUS status) {
  tBTA_HF_CLIENT evt;

  memset(&evt, 0, sizeof(evt));

  /* call app callback with open event */
  evt.open.status = status;
  bdcpy(evt.open.bd_addr, client_cb->scb.peer_addr);
  (*client_cb->p_cback)(BTA_HF_CLIENT_OPEN_EVT, &evt);
}

/*******************************************************************************
 *
 * Function         bta_hf_client_rfc_open
 *
 * Description      Handle RFCOMM channel open.
 *
 *
 * Returns          void
 *
 ******************************************************************************/
void bta_hf_client_rfc_open(tBTA_HF_CLIENT_DATA* p_data) {
  APPL_TRACE_DEBUG("%s", __func__);
  tBTA_HF_CLIENT_CB* client_cb =
      bta_hf_client_find_cb_by_handle(p_data->hdr.layer_specific);
  if (client_cb == NULL) {
    APPL_TRACE_ERROR("%s: cb not found for handle %d", __func__,
                     p_data->hdr.layer_specific);
    return;
  }

  bta_sys_conn_open(BTA_ID_HS, 1, client_cb->scb.peer_addr);

  bta_hf_client_cback_open(client_cb, BTA_HF_CLIENT_SUCCESS);

  /* start SLC procedure */
  bta_hf_client_slc_seq(client_cb, false);
}

/*******************************************************************************
 *
 * Function         bta_hf_client_rfc_acp_open
 *
 * Description      Handle RFCOMM channel open when accepting connection.
 *
 *
 * Returns          void
 *
 ******************************************************************************/
void bta_hf_client_rfc_acp_open(tBTA_HF_CLIENT_DATA* p_data) {
  APPL_TRACE_DEBUG("%s", __func__);
  tBTA_HF_CLIENT_CB* client_cb =
      bta_hf_client_find_cb_by_handle(p_data->hdr.layer_specific);
  if (client_cb == NULL) {
    APPL_TRACE_ERROR("%s: cb not found for handle %d", __func__,
                     p_data->hdr.layer_specific);
    return;
  }

  uint16_t lcid;
  BD_ADDR dev_addr;
  int status;

  /* set role */
  client_cb->scb.role = BTA_HF_CLIENT_ACP;

  APPL_TRACE_DEBUG("%s: serv_handle %d", __func__, client_cb->scb.serv_handle);

  /* get bd addr of peer */
  if (PORT_SUCCESS != (status = PORT_CheckConnection(client_cb->scb.serv_handle,
                                                     dev_addr, &lcid))) {
    APPL_TRACE_DEBUG("%s: error PORT_CheckConnection returned status %d",
                     __func__, status);
  }

  /* Collision Handling */
  if (alarm_is_scheduled(client_cb->scb.collision_timer)) {
    alarm_cancel(client_cb->scb.collision_timer);

    if (bdcmp(dev_addr, client_cb->scb.peer_addr) == 0) {
      /* If incoming and outgoing device are same, nothing more to do. */
      /* Outgoing conn will be aborted because we have successful incoming conn.
       */
    } else {
      /* Resume outgoing connection. */
      bta_hf_client_resume_open();
    }
  }

  bdcpy(client_cb->scb.peer_addr, dev_addr);

  /* do service discovery to get features */
  bta_hf_client_do_disc(client_cb);

  /* continue with open processing */
  bta_hf_client_rfc_open(p_data);
}

/*******************************************************************************
 *
 * Function         bta_hf_client_rfc_fail
 *
 * Description      RFCOMM connection failed.
 *
 *
 * Returns          void
 *
 ******************************************************************************/
void bta_hf_client_rfc_fail(tBTA_HF_CLIENT_DATA* p_data) {
  tBTA_HF_CLIENT_CB* client_cb =
      bta_hf_client_find_cb_by_handle(p_data->hdr.layer_specific);
  if (client_cb == NULL) {
    APPL_TRACE_ERROR("%s: cb not found for handle %d", __func__,
                     p_data->hdr.layer_specific);
    return;
  }

  /* reinitialize stuff */
  // client_cb->scb.conn_handle = 0;
  client_cb->scb.peer_features = 0;
  client_cb->scb.chld_features = 0;
  client_cb->scb.role = BTA_HF_CLIENT_ACP;
  client_cb->scb.svc_conn = false;
  client_cb->scb.send_at_reply = false;
  client_cb->scb.negotiated_codec = BTM_SCO_CODEC_CVSD;

  bta_hf_client_at_reset(client_cb);

  /* reopen server */
  bta_hf_client_start_server(client_cb);

  /* call open cback w. failure */
  bta_hf_client_cback_open(client_cb, BTA_HF_CLIENT_FAIL_RFCOMM);
}

/*******************************************************************************
 *
 * Function         bta_hf_client_disc_fail
 *
 * Description      This function handles a discovery failure.
 *
 *
 * Returns          void
 *
 ******************************************************************************/
void bta_hf_client_disc_fail(tBTA_HF_CLIENT_DATA* p_data) {
  tBTA_HF_CLIENT_CB* client_cb =
      bta_hf_client_find_cb_by_handle(p_data->hdr.layer_specific);
  if (client_cb == NULL) {
    APPL_TRACE_ERROR("%s: cb not found for handle %d", __func__,
                     p_data->hdr.layer_specific);
    return;
  }

  /* reopen server */
  bta_hf_client_start_server(client_cb);

  /* reinitialize stuff */

  /* call open cback w. failure */
  bta_hf_client_cback_open(client_cb, BTA_HF_CLIENT_FAIL_SDP);
}

/*******************************************************************************
 *
 * Function         bta_hf_client_open_fail
 *
 * Description      open connection failed.
 *
 *
 * Returns          void
 *
 ******************************************************************************/
void bta_hf_client_open_fail(tBTA_HF_CLIENT_DATA* p_data) {
  tBTA_HF_CLIENT_CB* client_cb =
      bta_hf_client_find_cb_by_handle(p_data->hdr.layer_specific);
  if (client_cb == NULL) {
    APPL_TRACE_ERROR("%s: cb not found for handle %d", __func__,
                     p_data->hdr.layer_specific);
    return;
  }

  /* call open cback w. failure */
  bta_hf_client_cback_open(client_cb, BTA_HF_CLIENT_FAIL_RESOURCES);
}

/*******************************************************************************
 *
 * Function         bta_hf_client_rfc_close
 *
 * Description      RFCOMM connection closed.
 *
 *
 * Returns          void
 *
 ******************************************************************************/
void bta_hf_client_rfc_close(tBTA_HF_CLIENT_DATA* p_data) {
  tBTA_HF_CLIENT_CB* client_cb =
      bta_hf_client_find_cb_by_handle(p_data->hdr.layer_specific);
  if (client_cb == NULL) {
    APPL_TRACE_ERROR("%s: cb not found for handle %d", __func__,
                     p_data->hdr.layer_specific);
    return;
  }

  /* reinitialize stuff */
  client_cb->scb.peer_features = 0;
  client_cb->scb.chld_features = 0;
  client_cb->scb.role = BTA_HF_CLIENT_ACP;
  client_cb->scb.svc_conn = false;
  client_cb->scb.send_at_reply = false;
  client_cb->scb.negotiated_codec = BTM_SCO_CODEC_CVSD;

  bta_hf_client_at_reset(client_cb);

  bta_sys_conn_close(BTA_ID_HS, 1, client_cb->scb.peer_addr);

  /* call close cback */
  (*client_cb->p_cback)(BTA_HF_CLIENT_CLOSE_EVT, NULL);

  /* if not deregistering reopen server */
  if (client_cb->scb.deregister == false) {
    /* Clear peer bd_addr so instance can be reused */
    bdcpy(client_cb->scb.peer_addr, bd_addr_null);

    /* start server as it might got closed on open*/
    bta_hf_client_start_server(client_cb);

    /* Make sure SCO is shutdown */
    bta_hf_client_sco_shutdown(client_cb);

    bta_sys_sco_unuse(BTA_ID_HS, 1, client_cb->scb.peer_addr);
  }
  /* else close port and deallocate scb */
  else {
    bta_hf_client_close_server(client_cb);
    bta_hf_client_scb_init();
    (*client_cb->p_cback)(BTA_HF_CLIENT_DISABLE_EVT, NULL);
  }
}

/*******************************************************************************
 *
 * Function         bta_hf_client_disc_int_res
 *
 * Description      This function handles a discovery result when initiator.
 *
 *
 * Returns          void
 *
 ******************************************************************************/
void bta_hf_client_disc_int_res(tBTA_HF_CLIENT_DATA* p_data) {
  uint16_t event = BTA_HF_CLIENT_DISC_FAIL_EVT;

  APPL_TRACE_DEBUG("%s: Status: %d", __func__, p_data->disc_result.status);
  tBTA_HF_CLIENT_CB* client_cb =
      bta_hf_client_find_cb_by_handle(p_data->hdr.layer_specific);
  if (client_cb == NULL) {
    APPL_TRACE_ERROR("%s: cb not found for handle %d", __func__,
                     p_data->hdr.layer_specific);
    return;
  }

  /* if found service */
  if (p_data->disc_result.status == SDP_SUCCESS ||
      p_data->disc_result.status == SDP_DB_FULL) {
    /* get attributes */
    if (bta_hf_client_sdp_find_attr(client_cb)) {
      event = BTA_HF_CLIENT_DISC_OK_EVT;
    }
  }

  /* free discovery db */
  bta_hf_client_free_db(p_data);

  /* send ourselves sdp ok/fail event */
  bta_hf_client_sm_execute(event, p_data);
}

/*******************************************************************************
 *
 * Function         bta_hf_client_disc_acp_res
 *
 * Description      This function handles a discovery result when acceptor.
 *
 *
 * Returns          void
 *
 ******************************************************************************/
void bta_hf_client_disc_acp_res(tBTA_HF_CLIENT_DATA* p_data) {
  tBTA_HF_CLIENT_CB* client_cb =
      bta_hf_client_find_cb_by_handle(p_data->hdr.layer_specific);
  if (client_cb == NULL) {
    APPL_TRACE_ERROR("%s: cb not found for handle %d", __func__,
                     p_data->hdr.layer_specific);
    return;
  }

  /* if found service */
  if (p_data->disc_result.status == SDP_SUCCESS ||
      p_data->disc_result.status == SDP_DB_FULL) {
    /* get attributes */
    bta_hf_client_sdp_find_attr(client_cb);
  }

  /* free discovery db */
  bta_hf_client_free_db(p_data);
}

/*******************************************************************************
 *
 * Function         bta_hf_client_rfc_data
 *
 * Description      Read and process data from RFCOMM.
 *
 *
 * Returns          void
 *
 ******************************************************************************/
void bta_hf_client_rfc_data(tBTA_HF_CLIENT_DATA* p_data) {
  tBTA_HF_CLIENT_CB* client_cb =
      bta_hf_client_find_cb_by_handle(p_data->hdr.layer_specific);
  if (client_cb == NULL) {
    APPL_TRACE_ERROR("%s: cb not found for handle %d", __func__,
                     p_data->hdr.layer_specific);
    return;
  }

  uint16_t len;
  char buf[BTA_HF_CLIENT_RFC_READ_MAX];
  memset(buf, 0, sizeof(buf));
  /* read data from rfcomm; if bad status, we're done */
  while (PORT_ReadData(client_cb->scb.conn_handle, buf,
                       BTA_HF_CLIENT_RFC_READ_MAX, &len) == PORT_SUCCESS) {
    /* if no data, we're done */
    if (len == 0) {
      break;
    }

    bta_hf_client_at_parse(client_cb, buf, len);

    /* no more data to read, we're done */
    if (len < BTA_HF_CLIENT_RFC_READ_MAX) {
      break;
    }
  }
}

/*******************************************************************************
 *
 * Function         bta_hf_client_svc_conn_open
 *
 * Description      Service level connection opened
 *
 *
 * Returns          void
 *
 ******************************************************************************/
void bta_hf_client_svc_conn_open(tBTA_HF_CLIENT_DATA* p_data) {
  tBTA_HF_CLIENT_CB* client_cb =
      bta_hf_client_find_cb_by_handle(p_data->hdr.layer_specific);
  if (client_cb == NULL) {
    APPL_TRACE_ERROR("%s: cb not found for handle %d", __func__,
                     p_data->hdr.layer_specific);
    return;
  }

  tBTA_HF_CLIENT evt;

  memset(&evt, 0, sizeof(evt));

  if (!client_cb->scb.svc_conn) {
    /* set state variable */
    client_cb->scb.svc_conn = true;

    /* call callback */
    evt.conn.peer_feat = client_cb->scb.peer_features;
    evt.conn.chld_feat = client_cb->scb.chld_features;

    (*client_cb->p_cback)(BTA_HF_CLIENT_CONN_EVT, &evt);
  }
}

#ifndef OCPP_AUTH_H
#define OCPP_AUTH_H

#include <stdbool.h>
#include "types/ocpp_authorization_data.h"

/** @file
 * @brief Contains functions for handeling authorization
 *
 * @details Implements behaviour based on configurations for Local auhorization list, Authorization cache,
 * offline authorization, online authorization, local pre authorization and authorization of unknown tags.
 * It supports all token authorization methods including parent id.
 */

/**
 * @brief change the ocpp configuration value LocalPreAuthorize for this component
 *
 * @param new_value the new configuration value
 */
void ocpp_change_local_pre_authorize(bool new_value);

/**
 * @brief change the ocpp configuration value LocalAuthorizeOffline for this component
 *
 * @param new_value the new configuration value
 */
void ocpp_change_authorize_offline(bool new_value);

/**
 * @brief change the ocpp configuration value LocalAuthListEnabled for this component
 *
 * @param new_value the new configuration value
 */
void ocpp_change_auth_list_enabled(bool new_value);

/**
 * @brief change the ocpp configuration value AuthorizationCacheEnabled for this component
 *
 * @param new_value the new configuration value
 */
void ocpp_change_auth_cache_enabled(bool new_value);

/**
 * @brief change the ocpp configuration value AllowOfflineTxForUnknownId for this component
 *
 * @param new_value the new configuration value
 */
void ocpp_change_allow_offline_for_unknown(bool new_value);

/**
 * @brief callback for authorization that may need to wait for CS response.
 *
 * @param id_token The token that has been authorized or rejected.
 */
typedef void (*authorize_cb)(const char * id_token);

/**
 * @brief callback for authorization comparisons that may need to wait for CS response.
 *
 * @param id_token_1 The token to compare with.
 * @param id_token_2 The token to compared to.
 */
typedef void (*authorize_compare_cb)(const char * id_token_1, const char * id_token_2);

/**
 * @brief Authorize using either Authorization cache, local Authorization list, or authorize.req.
 *
 * @param id_token The token to check for authorization
 * @param on_accept callback function to be called if authorization is accepted
 * @param on_deny callback function to be called if authorization is denied
 */
void ocpp_authorize(const char * id_token, authorize_cb on_accept, authorize_cb on_deny);

/**
 * @brief similar to ocpp_authorize but used to check if new token is authorized continue actions started by another token.
 *
 * @details If a token has started charging then a new token may be compared to check if new token is allowed to stop the transaction.
 * If The token is the same or if they share a parent token, then it will be accepted. A reserved CP may also accept comparable tokens.
 *
 * @param id_token_1 Token to compare with
 * @param parent_token_1 optional parent for id_token_1 if known
 * @param id_token_2 Token to compare to
 * @param parent_token_2 optional parent for id_token_2 if known
 * @param on_similar callback function to be called if tokens are comparable
 * @param on_different callback function to be called if tokens are not comparable
 */
void ocpp_authorize_compare(const char * id_token_1, const char * parent_token_1, const char * id_token_2, const char * parent_token_2,
			authorize_compare_cb on_similar, authorize_compare_cb on_different);

/**
 * @brief Returns the authorization status taking the potential tag expiration into account
 *
 * @paran id_tag_info The tag to get authorization status from
 */
enum ocpp_authorization_status_id ocpp_get_status_from_id_tag_info(struct ocpp_id_tag_info * id_tag_info);

/**
 * @brief Should be called when idTagInfo is recieved from CS
 *
 * @details ensures that authorization cach is up to date and informs CS if there is a
 * mismatch between recieved idTagInfo and idTagInfo in the authorization list.
 *
 * @param id_token the token identifying the idTagInfo
 * @param id_tag_info the received idTagInfo
 */
void ocpp_on_id_tag_info_recieved(const char * id_token, struct ocpp_id_tag_info * id_tag_info);

/**
 * @brief expects validated inputs equivalent to the ones gieven in SendLocalList.req
 *
 * @param version new listVersion expected after successfull update
 * @param update_full if true then the previous list will be overwritten, else a differential update will be made
 * @param auth_data localAuthorizationList as array of ocpp_authorization_data to update / write
 * @param list_length lenght of auth_data array
 */
enum ocpp_update_status_id ocpp_update_auth_list(int version, bool update_full, struct ocpp_authorization_data * auth_data, size_t list_length);

/**
 * @brief clears the authorization cache if it exists
 *
 * @return 0 on success or -1 on error.
 */
int ocpp_auth_clear_cache();

/**
 * @brief gets the version of the authorization list for GetLocalListVersion.req
 */
int ocpp_get_auth_list_version();

/**
 * @brief get a json object containing information about the ocpp authorization state.
 */
cJSON * ocpp_auth_get_diagnostics();

/**
 * @brief creates directory structure if not pressent and prepares for file synchronization.
 */
int ocpp_auth_init();

/**
 * @brief Frees variables allocated during ocpp_auth_init.
 */
void ocpp_auth_deinit();
#endif /* OCPP_AUTH_H */

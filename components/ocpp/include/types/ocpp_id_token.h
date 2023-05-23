#ifndef OCPP_ID_TOKEN_H
#define OCPP_ID_TOKEN_H

/** @file
* @brief Contains the OCPP type IdToken
*/

/**
 * @brief idToken is a case insensitive string
 *
 * @details Contains the identifier to use for authorization. It is a case insensitive string. In future releases this may become
 * a complex type to support multiple forms of identifiers.
 */
typedef char ocpp_id_token[21];

#endif /* OCPP_ID_TOKEN_H */

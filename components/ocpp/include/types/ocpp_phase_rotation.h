#ifndef OCPP_PHASE_ROTATION_H_
#define OCPP_PHASE_ROTATION_H_

/** @file
* @brief Contains the ConnectorPhaseRotation alternative values. OCPP does not define these as types, but use them as such.
*/

/** @name Phase rotation
* @brief "Phase rotation as used in ConnectorPhaseRotation configuration key"
*
* @details R can be identified as phase 1 (L1), S as phase 2 (L2), T as phase 3 (L3). If known, the Charge Point MAY also
* report the phase rotation between the grid connection and the main energy meter by using index number Zero (0).
* Values are reported in CSL, formatted: 0.RST, 1.RST, 2.RTS
*/
///@{
#define OCPP_PHASE_ROTATION_NOT_APPLICABLE "NotApplicable" ///< For single phase or DC Charge points
#define OCPP_PHASE_ROTATION_UNKNOWN "Unknown" ///< Not (yet) known
#define OCPP_PHASE_ROTATION_RST "RST" ///< Standard Reference Phasing
#define OCPP_PHASE_ROTATION_RTS "RTS" ///< Reversed Reference Phasing
#define OCPP_PHASE_ROTATION_SRT "SRT" ///< Reversed 240 degree rotation
#define OCPP_PHASE_ROTATION_STR "STR" ///< Standard 120 degree rotation
#define OCPP_PHASE_ROTATION_TRS "TRS" ///< Standard 240 degree rotation
#define OCPP_PHASE_ROTATION_TSR "TSR" ///< Reversed 120 degree rotation
///@}

/**
 * @brief Identifies tHE phase rotation
 */
enum ocpp_phase_rotation_id{
	eOCPP_PHASE_ROTATION_NOT_APPLICABLE,
	eOCPP_PHASE_ROTATION_UNKNOWN,
	eOCPP_PHASE_ROTATION_RST,
	eOCPP_PHASE_ROTATION_RTS,
	eOCPP_PHASE_ROTATION_SRT,
	eOCPP_PHASE_ROTATION_STR,
	eOCPP_PHASE_ROTATION_TRS,
	eOCPP_PHASE_ROTATION_TSR
};

/**
 * @brief converts phase_rotation to id
 *
 * @param  phase_rotation value to convert
 */
enum ocpp_phase_rotation_id ocpp_phase_rotation_to_id(const char * phase_rotation);

/**
 * @brief converts id to phase_rotation
 *
 * @param id phase_rotation id
 */
const char * ocpp_phase_rotation_from_id(enum ocpp_phase_rotation_id id);

#endif // OCPP_PHASE_ROTATION_H_

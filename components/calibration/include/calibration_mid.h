/* 
 * File:   MID.h
 * Author: Knut
 * MID Version: 0x01
 *
 * Created on May 7, 2018, 12:48 PM
 * Copyright Zaptec Charger AS - 2018
 */

#ifndef MID_H
#define	MID_H

#ifdef	__cplusplus
extern "C" {
#endif
    
// This header-file itself is legally relevant, but it exposes some methods that can be
// used by non-legally relevant code, and some methods that can only be used by legally relevant code
    
#define MID_STATUS_OK   0
#define MID_STATUS_NONE 0

/** Initialization **/
// MIDInit() has not been called, or did not complete successfully
#define MID_STATUS_NOT_INITIALIZED        (1L<<0)

// eMeter initialization failed
#define MID_STATUS_EMETER_ERROR           (1L<<1)
        
// Bootloader version is not compatible with legally relevant software
#define MID_STATUS_INVALID_BOOTLOADER     (1L<<2)
    
    
/** Flash page status **/
// Set if all MID pages are cleared (in production before calibration)
#define MID_STATUS_ALL_PAGES_EMPTY        (1L<<3)

// No valid MID pages were found at startup (none with valid checksums)
#define MID_STATUS_NO_VALID_PAGES         (1L<<4)

// The long-term storage event log was empty
#define MID_STATUS_LTS_EMPTY              (1L<<5)
    
/** Flash page content **/
// Illegal version in MID header
#define MID_STATUS_WRONG_VERSION          (1L<<6)
    
// Set if the device has not been calibrated in production
// Operation is still allowed, but the values can not be guaranteed
#define MID_STATUS_NOT_CALIBRATED         (1L<<7)

// Set if the calibration parameters have not been verified
#define MID_STATUS_NOT_VERIFIED           (1L<<8)

// Functional relay is not certified for MID (for legacy devices)
#define MID_STATUS_INVALID_FR             (1L<<9)

// The erase/write cycles of the flash has been exhausted
#define MID_STATUS_FLASH_EXHAUSTED        (1L<<10)
    
// Device-specific parameters checksum failed
#define MID_STATUS_PARAMETER_CRC_ERROR    (1L<<11)

// Integrity checks of the long-term storage event log failed
#define MID_STATUS_LTS_INTEGRITY          (1L<<12)
    
// The contents of the long-term storage event log were found to be invalid
#define MID_STATUS_LTS_INVALID            (1L<<13)
    
/** Dynamic errors **/
// The energy integrator is busy. If this flag is set, the integrator is busy processing new data, and the caller should retry the call later
#define MID_STATUS_INTEGRATOR_BUSY        (1L<<14)
    
// eMeter energy integrator hasn't been initialized correctly
#define MID_STATUS_INTEGRATOR_INIT        (1L<<15)
    
// eMeter energy integrator lost frames from eMeter
#define MID_STATUS_INTEGRATOR_LOST_FRAMES (1L<<16)
    
// eMeter energy integrator calculated too much energy between frames
#define MID_STATUS_INTEGRATOR_OVERFLOW    (1L<<17)
    
// Set if the non-volatile energy register is attempted to be updated with too much energy at once
#define MID_STATUS_UPDATE_OVERFLOW        (1L<<18)
    
// Set if the values stored in RAM does not match what was read back after writing to flash
#define MID_STATUS_WRITE_VERIFY_ERROR     (1L<<19)

// The MIDTick method has not been called at the required interval
#define MID_STATUS_TICK_TIMEOUT           (1L<<20)
    
// Something went wrong when initializing or updating the display
#define MID_STATUS_DISPLAY_ERROR          (1L<<21)
    
// Calibration parameters on eMeter doesn't match the expected values
#define MID_STATUS_INVALID_EMETER_PARAMS  (1L<<22)


// Even if these status bits are set, writing to the MID pages, and reading the cumulative energy register, is allowed
#ifdef ORIGIN
    // Maintain backwards-compatability with non-MID chargers that have not been calibrated
    #define MID_STATUS_NON_FATAL (MID_STATUS_INVALID_FR | MID_STATUS_NOT_CALIBRATED | MID_STATUS_NOT_VERIFIED)
#else
    // All costcut chargers should be calibrated and use MID functional relay boards
    #ifdef MID_COSTCUT_ALLOW_DEFAULT_CALIBRATION
        // Allow default calibration during development
        #define MID_STATUS_NON_FATAL (MID_STATUS_NOT_CALIBRATED | MID_STATUS_NOT_VERIFIED | MID_STATUS_INVALID_BOOTLOADER)
    #else
        // All status bits are normally fatal
        #define MID_STATUS_NON_FATAL (MID_STATUS_NONE)
    #endif
#endif

// Flags that are fatal for reading out nonvolatile data
#define MID_STATUS_FATAL_NONVOLATILE ( MID_STATUS_NONE \
    | MID_STATUS_NOT_INITIALIZED     \
    | MID_STATUS_ALL_PAGES_EMPTY     \
    | MID_STATUS_NO_VALID_PAGES      \
    | MID_STATUS_WRONG_VERSION       \
    | MID_STATUS_PARAMETER_CRC_ERROR \
    | MID_STATUS_WRITE_VERIFY_ERROR  \
    )

// Functional relay hardware versions
// Only the versions marked "MID Valid" is subject to MID certification.
// If any non-MID valid version is selected, a warning is raised in MIDStatus

#define FR_HW_VERSION_B5_0 0

#ifdef	__cplusplus
}
#endif

#endif	/* MID_H */


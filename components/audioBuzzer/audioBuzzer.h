#ifndef _AUDIOBUZZER_H_
#define _AUDIOBUZZER_H_

#ifdef __cplusplus
extern "C" {
#endif


void audioInit();

void audio_play_nfc_card_accepted();
void audio_play_nfc_card_accepted_debug();
void audio_play_nfc_card_denied();
void audio_play_single_biip();


#ifdef __cplusplus
}
#endif

#endif  /*_AUDIOBUZZER_H_*/

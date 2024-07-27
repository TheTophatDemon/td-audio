package tdaudio

/*
#include "./td_audio.h"
#include <stdlib.h>
*/
import "C"
import (
	"log"
	"unsafe"
)

type (
	SoundId uint32
	VoiceId struct {
		soundId SoundId
		voiceId uint32
	}
)

func Init() bool {
	return bool(C.td_audio_init())
}

func LoadSound(path string, polyphony uint8, looping bool, rolloff float32) SoundId {
	log.Println("Loading sound at ", path)
	cPath := C.CString(path)
	defer C.free(unsafe.Pointer(cPath))
	cSound := C.td_audio_load_sound(cPath, C.uint8_t(polyphony), C.bool(looping), C.float(rolloff))
	return SoundId(cSound.id)
}

func PlaySound(sound SoundId) VoiceId {
	return PlaySoundAttenuated(sound, 0.0, 0.0, 0.0)
}

func PlaySoundAttenuated(sound SoundId, x, y, z float32) VoiceId {
	cVoice := C.td_audio_play_sound(C.td_sound_id{id: C.uint32_t(sound)}, C.float(x), C.float(y), C.float(z))
	return VoiceId{
		soundId: SoundId(cVoice.sound_id.id),
		voiceId: uint32(cVoice.voice_id),
	}
}

func StopSound(voice VoiceId) {
	C.td_audio_stop_sound(C.td_voice_id{sound_id: C.td_sound_id{id: C.uint32_t(voice.soundId)}, voice_id: C.uint32_t(voice.voiceId)})
}

func SetListenerOrientation(posX, posY, posZ, dirX, dirY, dirZ float32) {
	C.td_audio_set_listener_orientation(C.float(posX), C.float(posY), C.float(posZ), C.float(dirX), C.float(dirY), C.float(dirZ))
}

func Teardown() {
	C.td_audio_teardown()
}

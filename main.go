package main

import (
	"bufio"
	"fmt"
	"io/fs"
	"miniaudio-experiment/tdaudio"
	"os"
)

func main() {
	fmt.Println("Blyat")

	tdaudio.Init()
	defer tdaudio.Teardown()

	tdaudio.LoadSound("assets/sounds/error.wav", 1, false, 0.0)

	sounds := make(map[string]tdaudio.SoundId)

	fs.WalkDir(os.DirFS("assets/sounds"), ".", func(path string, entry fs.DirEntry, err error) error {
		if entry.IsDir() {
			return nil
		}
		sounds[entry.Name()] = tdaudio.LoadSound("assets/sounds/"+path, 4, false, 0.0)
		return nil
	})

	sfx := sounds["opendoor.wav"]
	voice := tdaudio.PlaySound(sfx)
	reader := bufio.NewReader(os.Stdin)
	fmt.Println("Press enter to stop sound")
	_, _ = reader.ReadString('\n')
	tdaudio.StopSound(voice)
	fmt.Println("Press enter to exit...")
	_, _ = reader.ReadString('\n')
}

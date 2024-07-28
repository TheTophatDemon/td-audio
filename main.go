// This is an example program to help test the audio subsystem.
// Holding left click will move the 'player' around towards the mouse cursor.
// Sounds are attenuated relative to the player. If you don't hear anything, play it closer.
// Right click will spawn an entity that makes a positional sound.
// Middle click will toggle a looping sound that originates from the mouse cursor's position.
// Space will play a random loaded sound at full volume.

package main

import (
	"image"
	"image/color"
	_ "image/png"
	"io/fs"
	"log"
	"math"
	"math/rand/v2"
	"miniaudio-experiment/tdaudio"
	"os"
	"strings"
	"time"

	"github.com/hajimehoshi/ebiten/v2"
	"github.com/hajimehoshi/ebiten/v2/inpututil"
)

const (
	PLAYER_SPEED = 6.0
)

type Ent struct {
	x, y       float64
	dirX, dirY float64
	color      color.Color
}

type Game struct {
	sounds       map[string]tdaudio.SoundId
	playerImg    *ebiten.Image
	ents         []Ent
	loopingVoice tdaudio.VoiceId
}

func VecLen(x, y float64) float64 {
	return math.Sqrt(math.Pow(y, 2.0) + math.Pow(x, 2.0))
}

func VecNormalize(x, y float64) (float64, float64) {
	ln := VecLen(x, y)
	if ln == 0.0 {
		return 0.0, 0.0
	}
	return x / ln, y / ln
}

func NewGame() *Game {
	game := &Game{
		sounds: make(map[string]tdaudio.SoundId),
		ents:   make([]Ent, 1, 10),
	}

	game.ents[0] = Ent{
		x: 40.0, y: 40.0,
		dirX: 1.0, dirY: 0.0,
		color: color.RGBA{R: 255, G: 0, B: 0, A: 255},
	}

	{
		// Load player image
		playerImgFile, err := os.Open("assets/textures/player.png")
		if err != nil {
			panic(err)
		}
		defer playerImgFile.Close()
		playerImg, _, err := image.Decode(playerImgFile)
		game.playerImg = ebiten.NewImageFromImage(playerImg)
	}

	profStart := time.Now()

	// The first sound loaded acts as the placeholder in case of errors.
	tdaudio.LoadSound("assets/sounds/error.wav", 1, false, 0.0)
	fs.WalkDir(os.DirFS("assets/sounds"), ".", func(path string, entry fs.DirEntry, err error) error {
		if entry.IsDir() {
			return nil
		}
		game.sounds[entry.Name()] = tdaudio.LoadSound("assets/sounds/"+path, 4, strings.HasSuffix(path, "_looped.wav"), 0.05)
		return nil
	})

	profDur := time.Now().Sub(profStart)
	log.Printf("Loading all sounds took: %v milliseconds.\n", profDur.Milliseconds())

	return game
}

func (game *Game) Layout(width, height int) (int, int) {
	return width, height
}

func (game *Game) Update() error {
	mouseX, mouseY := ebiten.CursorPosition()
	player := &game.ents[0]

	tdaudio.SetListenerOrientation(float32(player.x), 0.0, float32(player.y), float32(player.dirX), 0.0, float32(player.dirY))
	if tdaudio.SoundIsPlaying(game.loopingVoice) {
		tdaudio.SetSoundPosition(game.loopingVoice, float32(mouseX), 0.0, float32(mouseY))
	}

	if ebiten.IsMouseButtonPressed(ebiten.MouseButton0) {
		player.dirX, player.dirY = VecNormalize(float64(mouseX)-player.x, float64(mouseY)-player.y)
		player.x += player.dirX * PLAYER_SPEED
		player.y += player.dirY * PLAYER_SPEED
	} else if inpututil.IsMouseButtonJustPressed(ebiten.MouseButton1) {
		var zero tdaudio.VoiceId
		if game.loopingVoice == zero {
			game.loopingVoice = tdaudio.PlaySoundAttenuated(game.sounds["sickle_looped.wav"], float32(mouseX), 0.0, float32(mouseY))
		} else {
			tdaudio.StopSound(game.loopingVoice)
			game.loopingVoice = zero
		}
	} else if inpututil.IsMouseButtonJustPressed(ebiten.MouseButton2) {
		ent := Ent{
			x: float64(mouseX), y: float64(mouseY),
		}
		ent.dirX, ent.dirY = VecNormalize(rand.Float64()-0.5, rand.Float64()-0.5)
		ent.color = color.RGBA{R: uint8(rand.UintN(255)), G: uint8(rand.UintN(255)), B: uint8(rand.UintN(255)), A: 255}
		game.ents = append(game.ents, ent)

		tdaudio.PlaySoundAttenuated(game.sounds["wraith0.wav"], float32(ent.x), 0.0, float32(ent.y))
	}

	if inpututil.IsKeyJustPressed(ebiten.KeySpace) {
		// Choose a random loaded sound and play it unattenuated.
		keys := make([]string, 0, len(game.sounds))
		for key := range game.sounds {
			if key != "error.wav" && !tdaudio.SoundIsLooping(game.sounds[key]) {
				keys = append(keys, key)
			}
		}
		index := int(rand.UintN(uint(len(keys))))
		tdaudio.PlaySound(game.sounds[keys[index]])
	}
	return nil
}

func (game *Game) Draw(screen *ebiten.Image) {
	screen.Fill(color.RGBA{0x00, 0x00, 0x55, 0xFF})

	for _, ent := range game.ents {
		op := &ebiten.DrawImageOptions{}
		op.GeoM.Translate(-float64(game.playerImg.Bounds().Dx())/2.0, -float64(game.playerImg.Bounds().Dy())/2.0)
		op.GeoM.Scale(2.0, 2.0)
		op.GeoM.Rotate(math.Atan2(ent.dirY, ent.dirX))
		op.GeoM.Translate(ent.x, ent.y)
		op.ColorScale.ScaleWithColor(ent.color)
		screen.DrawImage(game.playerImg, op)
	}
}

func main() {
	tdaudio.Init()
	defer tdaudio.Teardown()

	ebiten.SetWindowSize(800, 480)
	ebiten.SetWindowTitle("TD Audio Test")

	if err := ebiten.RunGame(NewGame()); err != nil {
		log.Fatal(err)
	}
}

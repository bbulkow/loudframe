# loudframe
This project is to integrate esp32 and audio playing, in a low cost package, for use in art projects.

## version

esp-idf 5.4 is mostly used. esp-adf was used with its linked esp-idf (see below). Dev env was Windows.

# genesis

For 2019 burning man, I had created a modest soundfield for The Flaming Lotus Girl's serenity project. I based that on Raspberry PI but
had two main problems. Dealing with Alsa and USB, with a multi-USB system, was difficult. It was hard to label and keep track of the outputs.
Or even to get it working, originally, at all. Second problem was price. Each RPI had to be used for multiple outputs, and higher quality
USB to sound units were fairly expensive, although with some lead time and experimentation I could probably get that sorted. The good news
was RPI and ethernet.

Recently, I have been interested in a greatly simplified build: using esp32, proximity sensor, and potentially my own design of
speakers and drivers, I could get higher quality and more diversity of application.

Simultaneously, I ran into someone who had an interesting project that would be best served with
a specialized picture frame with speakers in it. The goal would be to also look good - low profile -
and would need to have a proximity sensor. That project wasn't bound by the build constraints of
burning man (more dust less money), but it seemed interesting to bring these two together.

Thus, LOUD FRAME.

# ESP32 Audio Hardware

In general, the method to play higher quality audio in an ESP32 is to use a chip designed for that (or bluetooth).

Hardware chips generally have two parts, I2C and I2S. I2C is used for control - setting bit rate and output volume
and whatnot - and I2S is used for the sound itself. It's a higher bandwidth protocol without the complexity 
of addressing and slaves and whatnot.

I foudn the "ESP32-Audio-Kit" dev kit. It is built as a less expensive device than the Lyra and other cards.
The "A1S" version fo ESP32 has an audio chip built inside. 

Complexity comes because the "ESP32-audio-kit" was open sourced by "AI Thinker", which means there are various offb
brand version of it available, with some differences in pinout. But it turns out that's kinda sorted out.

The `play_sdcard` directory uses ESP-ADF , but it has a terrible problem with glitching.

The `idf-player` directory is a more direct attempt using esp-idf 5.4 and the thinker card.

If that doesn't work, I'll probably cobble together an esp32 specific solution without the audio-kit card.

# Audio

The first version simply needs to play a single file in a loop. The use of SD card is a nice idea, because it makes
the storage virtually unlimited, and provides a "sneakernet" method for distributing files.

At first, I have been trying ESP-ADS, but even in the simplest possible project, it has a glitching issue.
Every few minutes it has about a 3 second glitch. Since I have the ESP-ADF abstraction, it's not simple yet
for me to root-cause and fix.

I have a couple of sound alternatives on ESP32. One is the "AI Thinker" board, which is overkill, but has so many components
that are nice, at a reasonable price, that I'm trying it first. The AI thinker is about $12 from US sources and $4 from Aliexpress,
and has an SD card, an integrated sound output, audio jacks. It also has too many things I don't need - two microphones and a lot of switches and a bigger speaker amp.

If that doesn't work out, I can use a more generic ESP32 and any of a number of different sound breakout boards.

# Proximity sensors

In a gallery environment, it would be really cool to have a painting get loud when you come up to it. It's unclear if this 
will *really* work in practice, because how annoying would depend on how full the gallery is. I have seen 
museums with proximity-based headphone systems (eg, Pheonix AZ's musical instrument museum), but that seems cumbersom.

In some basic testing the proximity sensors best for the job appear to be ultrasonic. Notably the Maxbotics (high price) and the us100 (low price and bigger). However, it's really unclear if ultrasonic will work in a high-dust environment. I also tried
two lidar-type sensors available from adafruit. One appears to be VERY columnuar.

# Directories

## gyus42

This ultrasonic sensor was designed as a light-weight system for model airplanes, and is supported by
a common modle aviation controller.

A simple ESP-IDF project that displays distances using the sensor.

Sensor appears to be not very accurate against human-type sizes distances. Price is also fairly high at $15 each.

## us100

This sensor appears to be much better, at a reasonable price (more like $6 each).

## Maxbotics

While this is the "market leader" with a large number of versions, and very easy methods to connect, the
price is fairly high for breakout boards, at $30 each.

This code uses the serial interface system. Beware, it also has a system of averaging, but does print
out the raw received values too.

## play_sdcard

This is an ESP-ADF project, based on the example code, to play a simple file from an SD card. It has a slight improvement
because it will sense the type of the file and play it correctly (at least for WAV and MP3).


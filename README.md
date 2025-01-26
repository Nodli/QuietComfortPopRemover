# QuietComfortPopRemover
 
The Bose QuietComfort Headphones have an issue that creates hearable pops when you start / stop an audio stream without anything else playing.
The issue can be fixed by playing a never ending stream of empty audio data.
You can use a black screen video from wherever but that means opening a browser, remembering to start it every time you use your headphones and it does not detect when you disconnect / reconnect the headphones.
This project generates a small executable that watches audio devices being connected / disconnected to your computer.
When it detects that Bose QuietComfort Headphones are connected, it streams empty audio data to them until they are disconnected.
That means you can put the .exe as a startup program and forget about everything.
Nice !

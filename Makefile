build:
	g++ -o prismwm prism.cpp config.cpp launch.cpp monitor.cpp window.cpp lock.cpp paper.cpp -lX11 -lXrandr -lXft -I/usr/include/freetype2 -lpam

clean:
	sudo rm -r prismwm
all: event_driven_server local_server driver

event_driven_server: event_driven_server.c
	gcc -o event_driven_server event_driven_server.c

local_server: local_server.c
	gcc local_server.c -o local_server

driver: driver.c nmb.c nmb.h
	gcc -o driver driver.c nmb.c nmb.h

clean:
	rm event_driven_server driver local_server -rf
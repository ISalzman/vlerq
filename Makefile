# launch as web server
run:
	./app.tcl

# run all tests
test:
	tests/all.tcl

# called on F6 by TextMate on MacOSX to refresh the current window in Camino
testmate:
	osascript -e 'tell application "Camino"' \
		  -e 'open location (get URL of current tab of window 1)' \
	  	  -e 'end tell'

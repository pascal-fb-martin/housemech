# This script is intended for testing HouseMech.
# The HouseSimio service must be running (and serving points feed1 and feed2).

proc EVENT.SCRIPT.mechrules.tcl {action} {
    puts "================ Script mechrules.tcl $action completed"
    House::control start feed2
}

proc POINT.feed2 {state} {
    puts "================ Control feed2 changed to $state"
    puts "================ State of point feed1 is [House::control state feed1]"
    if {$state == "on"} {
        puts "================ Activating point feed1"
        House::control start feed1 10 "ON feed2 CHANGE TO ON"
        House::event new POINT feed1 PULSE "AFTER DETECTING feed2"
    }
    puts "================ Last action for event SCRIPT mechrules.tcl: [House::event state SCRIPT mechrules.tcl]"
}

proc POINT.feed1 {state} {
    switch -exact $state {
        on {
            House::control cancel feed2 "ON feed1 CHANGE to ON"
        }
        off {
            # Don't do an exit like this in a real "production" trigger..
            puts "================ Exit controlled by feed1"
            exit 0
        }
    }
}


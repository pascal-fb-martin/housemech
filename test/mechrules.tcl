# This script is intended for testing HouseMech.
# Dependencies:
# The Housesaga service must be running.
# The HouseDepot service must be running and hold this script.
# The HouseSimio service must be running and serving points mech1 and mech2.

proc EVENT.SCRIPT.mechrules.tcl {action} {
    puts "================ Script mechrules.tcl $action completed"
    puts "Sunset: [clock format [House::sunset]]"
    puts "Sunrise: [clock format [House::sunrise]]"
    set now [clock seconds]
    if {$now < [House::sunset]} {
        puts "Still daytime"
    } elseif {$now > [House::sunrise]} {
        puts "Night is over"
    } else {
        puts "Night time"
    }
    House::control start mech2
}

proc POINT.mech2 {state} {
    puts "================ Control mech2 changed to $state"
    puts "================ State of point mech1 is [House::control state mech1]"
    if {$state == "on"} {
        puts "================ Activating point mech1"
        House::control start mech1 10 "ON mech2 CHANGE TO ON"
        House::event new POINT mech1 PULSE "AFTER DETECTING mech2"
    }
    puts "================ Last action for event SCRIPT mechrules.tcl: [House::event state SCRIPT mechrules.tcl]"
}

proc POINT.mech1 {state} {
    switch -exact $state {
        on {
            House::control cancel mech2 "ON mech1 CHANGE to ON"
        }
        off {
            # Don't do an exit like this in a real "production" trigger..
            puts "================ Exit controlled by mech1"
            exit 0
        }
    }
}


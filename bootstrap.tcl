# HouseMech bootstrap script.
#
# This script is meant to set the Tcl environment required to run the Tcl rules.

namespace eval House {

    namespace export state

    variable EventState

    proc event {cmd category name {action {}} {description ""}} {

        variable EventState

        switch -exact $cmd {
            state {
                set index "${category}.${name}.action"
                if {$action != {}} {
                    set EventState($index) $action
                }
                if {[info exists EventState($index)]} {
                    return $EventState($index)
                }
            }

            new {
                nativeevent $category $name $action $description
                if {$action != {}} {
                    set index "${category}.${name}.action"
                    set EventState($index) $action
                }
            }
        }
        return {}
    }
}


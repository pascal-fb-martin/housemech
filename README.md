# HouseMech

A home web service to automate actions on external events.

## Overview

A micro service that executes user scripts as triggers on specific events or control point changes.

Any event initiated by any House service can be used as a trigger. This includes events initiated by a CCTV service (e.g. HouseMotion): lights, or other devices, may be turned on and off based on camera motion detection.

This project depends on [echttp](https://github.com/pascal-fb-martin/echttp) and [houseportal](https://github.com/pascal-fb-martin/houseportal). It accepts all standard options of echttp and the houseportal client runtime. See these two projects for more information.

## Command line options.

The HouseMech service is entirely configured from a "mechrules.tcl" script. That script must be uploaded to the "scripts" repository of the HouseDepot service. This can be done using the `housedepositor` command.

## Installation

To install, follow the steps below:

* Install gcc, git, openssl (libssl-dev), tcl-dev.
* Install [echttp](https://github.com/pascal-fb-martin/echttp)
* Install [houseportal](https://github.com/pascal-fb-martin/houseportal)
* Clone this repository.
* make rebuild
* sudo make install

## Writing an automation script.

This service loads a Tcl automation script from HouseDepot, repository "scripts" and name "mechrules.tcl".

This script defines triggers that HouseMech will call when a matching event, sensor data or control point change is detected. A trigger is a Tcl proc which name matches the source that caused the trigger. The match between the source and the Tcl proc is entirely based on the proc name.

### Event triggers

This service uses 3 fields from an House event record: category, name and action.

```
proc EVENT._category_._name_._action_ {} {
    ...
}
```

This trigger is called when an event occurs and its category, name and action fields all match the proc name.

```
proc EVENT._category_._name_ {action} {
    ...
}
```

This trigger is called when an event occurs, both its category and name fields match the proc name, and there was no more specific trigger defined.

```
proc EVENT._category_ {name action} {
    ...
}
```

This trigger is called when an event occurs and its category field matches the proc name, and there was no more specific trigger defined.

### Sensor triggers

This service uses 3 fields from an House sensor record: location, name and value.

```
proc SENSOR._location_._name_ {value} {
    ...
}
```

This trigger is called when new data is available from the specified sensor and both the sensor's location and name fields match the proc name.

```
proc SENSOR._name_ {location value} {
    ...
}
```

This trigger is called when new data is available from the specified sensor, the sensor's name field matches the proc name, and there was no more specific trigger defined.

### Control point triggers

```
proc POINT._name_ {state} {
    ...
}
```

This trigger is called when a control point's state changes and the point name matches the proc name.

The same event detection will activate at most one trigger: HouseMech will choose the more specific trigger proc that matches, and ignore all other trigger procs.

Here is an example of two triggers; the first trigger is activated upon any service event and the second trigger is activated upon state change for the control point named "testpoint":

```
proc EVENT.SERVICE {name action} {
    puts "================ Service $name $action detected"
}

proc POINT.testpoint {state} {
    puts "================ Control point testpoint changed to $state"
}
```

### HouseMech Tcl API

An automation trigger script may access the following Tcl commands:

```
House::event {"state" category name}
```

This returns the last detected action for the specified event. This can be used to execute some logic only when multiple conditions are met, i.e. after detection of a sequence or combination of events.

```
House::event {"new" category name {action ""} {description ""}}
```

This generates a new event.

```
House::control {["verbose"] "set" name state {pulse 0} {reason "HOUSEMECH TRIGGER"}}
```

This sets the specified control point to the specified state. If the pulse parameter is present, it represents how long the control point must remain in the specified state. Once the pulse has expired, the point state automatically changes to `off`. The reason parameter will be reflected in any subsequent event related to this command. if the first parameter is `verbose`, an event is generated locally that describes the control to be activated. This can help with troubleshooting when controls do not go through.

```
House::control {["verbose"] "start" name {pulse 0} {reason "HOUSEMECH TRIGGER"}}
```

This is equivalent to the `set` command above with `state` set to `on`.

```
House::control {"cancel" name {reason "HOUSEMECH TRIGGER"}}
```

This sets the specified control point to "off". The reason parameter will be reflected in all subsequent events related to this command.

```
House::control {"state" name}
```

This returns the known state of the control point, or an empty string if the state is not known. There might be a delay between executin a control and the state changing: the state always represent the actual state of the control point as reported by the service handling the point.

```
House::sunset
```

This returns the sunset time for the upcoming night (in daytime) or current night (in night time).

```
House::sunrise
```

This returns the sunrise time for the upcoming night (in daytime) or current night (in night time).

It is night time if `( [clock seconds] > [House::sunset] ) && [clock seconds] < [House::sunrise] )`.

## Note about Motion Detection

If a light is turned on or off based on camera motion detection, it is imperative to test for, and avoid, any unintended feedback loop, as the control of the light might trigger a new motion detection.

When using the Motion project coupled with the HouseMotion service, it is recommended to use the `lightswitch_percent` and `lightswitch_frames` options in file motion.conf to block the feedback. These two options are meant to prevent motion detection when a wide change of luminosity occurs, like at sunset or sunrise, or when a light turns on or off.

A similar feedback loop could occur when using other light-based motion detection devices, such as an infrared sensor.

## Interface

This service does not really have a web interface at this time, beside accessing its internal events.

## Test

The HouseDepot service must be running.

The HouseSaga service must be running (no special configuration is needed).

On a separate terminal, load the SimIO test configuration _from the housetest project_ in HouseDepot and then run the housesimio service:

```
housedepositor config simio.json simio.json
./housesimio
```

Load the HouseMech test rules in HouseDepot:

```
housedepositor scripts mechrules.tcl test/mechrules.tcl
```

Run HouseMech using the `test/runmech` script. The test will stop on its own when the success criteria defined in `test/mechrules.tcl` will be met.

## Debian Packaging

The provided Makefile supports building private Debian packages. These are _not_ official packages:

- They do not follow all Debian policies.

- They are not built using Debian standard conventions and tools.

- The packaging is not separate from the upstream sources, and there is
  no source package.

To build a Debian package, use the `debian-package` target:

```
make debian-package
```


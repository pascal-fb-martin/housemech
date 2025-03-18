# HouseMech

A home web service to automate actions on external events.

## Overview

A micro service that executes user scripts as triggers on specific events or control point changes.

The plan is to add more external trigger and action types in the future (like starting events in Motion).

This project depends on [echttp](https://github.com/pascal-fb-martin/echttp) and [houseportal](https://github.com/pascal-fb-martin/houseportal). It accepts all standard options of echttp and the houseportal client runtime. See these two projects for more information.

## Command line options.

The HouseMech service is entirely configured from a "mechrules.tcl" script. That script must be uploaded to the "scripts" repository of the HouseDepot service. This can be done using the `housedepositor` command.

## Installation

To install, following the steps below:
* Install gcc, git, openssl (libssl-dev), tcl-dev.
* Install [echttp](https://github.com/pascal-fb-martin/echttp)
* Install [houseportal](https://github.com/pascal-fb-martin/houseportal)
* Clone this repository.
* make rebuild
* sudo make install

## Writing an automation script.

This service loads a Tcl automation script from HouseDepot, repository "scripts" and name "mechrules.tcl".

This script defines triggers that HouseMech will call when a matching event or control point change occurs. A trigger is a Tcl proc which name matches the source that caused the trigger. The match between the source and the Tcl proc is entirely based on the proc name.

This service uses 3 fields from an House event record: category, name and action.

The following types of trigger procs can be defined:

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

An automation trigger script may access the following Tcl commands:

```
House::event {"state" category name}
```
This returns the last detected action for the specified event. This can be used to execute some logic only when multiple conditions are met, i.e. after detection of a sequence  or combination of events.

```
House::event {"new" category name {action ""} {description ""}}
```
This generates a new event.

```
House::control {"start" name {pulse 0} {reason "HOUSEMECH TRIGGER"}}
```
This sets the specified control point to "on". If the pulse parameter is present, it represent for how long the control point must remain on. The reason parameter will be reflected in all subsequent event related to this command.

```
House::control {"cancel" name {reason "HOUSEMECH TRIGGER"}}
```
This sets the specified control point to "off". The reason parameter will be reflected in all subsequent events related to this command.

```
House::control {"state" name}
```
This returns the known state of the control point, or an empty string if the state is not known. There might be a delay between executin a control and the state changing: the state always represent the actual state of the control point as reported by the service handling the point.

## Interface

This service does not really have a web interface at this time, beside accessing its internal events.


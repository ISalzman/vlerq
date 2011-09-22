#!/usr/bin/env tclkit85
# main application startup script

proc tclLog {msg} { Log: {$msg} }

source [file dir [info script]]/code/Rig.tcl

Rig autoload [Rig home]/rigs http://contrib.mavrig.org/

Rig notify main.Init $argv
Rig notify main.Run
if {![info exists Rig::exit]} { vwait Rig::exit }
Rig notify main.Done $Rig::exit
exit $Rig::exit

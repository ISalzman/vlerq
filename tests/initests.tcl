# initests.tcl -- common test setup script

if {![info exists testsInited]} {
  if {"::tcltest" ni [namespace children]} {
    package require tcltest
    namespace import tcltest::*
  }

  # singleProcess true ;# run without forking
  testsDirectory [file dir [info script]]

  # if run from the tests directory, move one level up
  if {[pwd] eq [testsDirectory]} {
    cd ..
  }

  temporaryDirectory [pwd]
  workingDirectory [file dir [testsDirectory]]

  testConstraint vfs [expr {![catch { package require vfs }]}]

  # load Rig.tcl and all rigs next to it
  # must disable tclLog to avoid "Test file error: notify rig.Loaded {...}" msg
  proc tclLog {msg} {}
  source code/Rig.tcl

  set testsInited 1
}

# General machinery for the entire application
variable ~ {$Id$}

# Event triggers:
#   rig.Fetching $name $path $url - signals that a module will be downloaded
#   rig.Loaded $modules - after all setup, if any modules were (re-)loaded
#XXX rig.Defined name path - triggered for each optional module definition
#XXX rig.Started timestamp - first event fired

# wrap this code in an explicit namespace so it can be sourced from Tcl
namespace eval ::Rig {

namespace eval v {
  variable mainscript ;# full path of the file which sourced this Rig.tcl file
  variable rigdir     ;# will be set to dir of this Rig.tcl source file
  variable lastreload ;# track last reload time to only reload changes files
  variable hooks      ;# array, per event a sorted list of {prio cmd} hooks
  variable tagcache   ;# tracepoint array, caches file spots for speed
  variable config     ;# array with configuration settings
}

# tracepoint data, one array per source file, key = procname, value = linenum
namespace eval t {}

# return the application home directory
proc home {} {
  return [file dir $v::mainscript]
}

# documentation handler
proc doc {about {details {}}} {
  # this is just a placeholder for now
}

# interface to the msgcat package
proc msg {cmd args} {
  package require msgcat
  uplevel [list msgcat::mc$cmd {*}$args]
}

# simplified message catalog definition (removes comments, adds list structure)
proc msgdefs {locale defs} {
  set defs [regsub -line -all {^\s*(\S+)\s+(.+)$} [uncomment $defs] {\1 {\2}}]
  uplevel [list msgcat::mcmset $locale $defs]
}

# remove comment lines
proc uncomment {text} {
  return [regsub -line -all {^\s*#.*$} $text {}]
}

# simple error handler, logs and continues
proc bgerr {msg} {
  #event error $msg
  if {![string match $msg* $::errorInfo]} {
    tclLog [string repeat = 65]
    tclLog $msg
  }
  tclLog [string repeat = 65]
  tclLog $::errorInfo
  tclLog [string repeat . 65]
}

# run arguments as cmd, but catch/report/ignore errors (using bgerror)
proc catcher {args} {
  if {[catch { uplevel $args } result]} {
    bgerror "Failed call: $args"
  } else {
    return $result
  }
}

# get variable value or use a default
proc get {vname {default ""}} {
  upvar $vname v
  if {![info exists v] || $v eq ""} { return $default }
  return $v
}

# assertion checking, can be disabled by re-defining this
proc check {cond} {
  if {![uplevel [list expr $cond]]} {
    set ex [uplevel [list subst $cond]]
    tclLog "check {$cond} failed: $ex"
  }
}

# return true if the specified command exists
proc defined? {cmd} {
  return [expr {[uplevel [list info commands $cmd]] ne ""}]
}

# run script if the specified command does not exist
proc ifdef {cmd script} {
  if {[uplevel [list info commands $cmd]] ne ""} {
    uplevel $script
  }
}

# run script if the specified command does not exist
proc ifndef {cmd script} {
  if {[uplevel [list info commands $cmd]] eq ""} {
    uplevel $script
  }
}

# return a nicely formatted time stamp
proc timestamp {} {
  set milli [clock milliseconds]
  set secs [expr {$milli/1000}]
  return [clock format $secs -format %H:%M:%S].[string range $milli end-2 end]
}

# return a date in various formats
proc date {{type ""} {secs ""}} {
  # if type contains a %, use it as format without further lookup
  # also consider using msgcat for some, as diff sessions may need diff formats
  if {$secs eq ""} {
    set secs [clock seconds]
  }
  switch -glob $type {
    ""      { set fmt "" }
    *%*     { set fmt [list -format $type ] }
    http    { set fmt {-format {%a, %d %b %Y %T %Z}} }
    iso     { set fmt {-format %Y-%m-%dT%H:%M:%S} }
    isoUTC  { set fmt {-format %Y-%m-%dT%H:%M:%SZ -gmt 1} }
    default { error $type? }
  }
  return [clock format $secs {*}$fmt]
}

# return file contents as a text string or empty if the file doesn't exist
proc readfile {name} {
  if {[file exists $name]} {
    set fd [open $name]
    fconfigure $fd -encoding utf-8
    set data [read $fd]
    close $fd
    return $data
  }
}

# return contents of a text file, obtained from a remote http url
proc readhttp {url args} {
  package require http
  set t [http::geturl $url {*}$args]
  if {[http::status $t] ne "ok" || [http::ncode $t] != 200} {
    set r [http::code $t]
    http::cleanup $t
    error "unexpected reply: $r"
  }
  set r [http::data $t]
  http::cleanup $t
  return $r
}

# save a string as text file
proc writefile {name content} {
  file mkdir [file dir $name]
  set fd [open $name w]
  fconfigure $fd -encoding utf-8
  puts -nonewline $fd $content
  close $fd
}

# log application activity
proc log {msg} {
  puts "[timestamp]  [uplevel [list subst $msg]]"
}

# get app-wide configuration defaults, or return specified default if empty
proc config {{name ""} {default ""}} {
  if {$name ne ""} {
    return [get v::config($name) $default]
  }
  return [array get v::config]
}

# process command-line options, which must be pairs of the form: -name value
proc options {opts {argv ""}} {
  foreach {option value} [uncomment $opts] {
    set i [lsearch $argv -$option]
    if {$i >= 0} {
      set value [lindex $argv $i+1]
    }
    set v::config($option) $value
  }
}

# load/reload all sources in the same dir as Rig.tcl
proc reload {} {
  set now [clock seconds]
  # reload all changed files first
  foreach f [lsort [glob $v::rigdir/*.tcl]] {
    if {[file mtime $f] > $v::lastreload} {
      if {[file tail $f] eq "main.tcl"} continue
      set ns [file root [file tail $f]]
      # files foo~bar.tcl are loaded into foo namespace, but only if present
      if {![regsub {~.*} $ns {} ns]} {
        namespace eval ::$ns {
          namespace export -clear {[a-z]*}
          namespace ensemble create
        }
        lappend modules $ns
      } elseif {![namespace exists ::$ns]} {
        tclLog "[file tail $f]: skipped, module $ns doesn't exist"
        continue
      }
      namespace inscope ::$ns source $f
    }
  }
  # trigger events after all sources have been reloaded
  if {[info exists modules]} {
    set v::lastreload $now
    notify rig.Loaded $modules
  }
}

# register a hook command so it gets called when the named event fires
proc hook {event cmd {prio 5}} {
  upvar 0 v::hooks($event) hooks
  set cmd [uplevel [list namespace code $cmd]]
  lappend hooks [list $prio $cmd]
  #TODO lsort -unique -index 0 ? cmd shouldn't appear twice, even with diff prio
  #TODO consider switching to dict format, i.e. pairwise, sorted prio as key
  set hooks [lsort -dict [lsort -unique $hooks]]
}

# unregister a previously registered hook command
proc unhook {event cmd} {
  upvar 0 v::hooks($event) hooks
  set hooks [lsearch -inline -not -all [get hooks] [list * $cmd]]
}

# trigger an event, calling all registered hooks for it with specified args
proc notify {event args} {
  tclLog "notify $event $args"
  upvar 0 v::hooks($event) hooks
  set result 0
  #TODO rework this logic, return value should be last real one, for call uses
  foreach pair [get hooks] {
    set cmd [lindex $pair 1]
    switch [catch { {*}$cmd {*}$args } r] {
      0 { set result 1; continue }
      2 { return 1 ;# return }
      3 { return 0 ;# break }
      default {
        tclLog "hook $event failed: $::errorInfo"
        unhook $event $cmd
      }
    }
  }
  return $result
}

# set a tracepoint for optional tracking
proc tag {args} {
  # type source line 12 file {/.../blah.tcl} cmd {@ 1} proc ::xxx::a level 1
  set frame [info frame -1]
  set file [dict get $frame file]
  set line [dict get $frame line]
  upvar 0 v::tagcache($file,$line) spot
  if {![info exists spot]} {
    set spot [file tail $file]([namespace tail [dict get $frame proc]])
  }
  set t::$spot $line
}

# do something with tracepoints each time they are executed
proc tracepoint {type mask args} {
  trace add var t::$mask write [namespace code [list Trace/$type {*}$args]]
}

# log tracepoints generate log entries and evaluate the args as vars or msgs
proc Trace/log {a e op} {
  upvar frame frame
  upvar args args
  append loc [namespace tail [dict get $frame proc]] / \
              [file root [file tail [dict get $frame file]]] . [set ${a}($e)]
  set info {}
  foreach a $args {
    if {[uplevel 2 [list info exists $a]]} {
      lappend info ${a}: [uplevel 2 [list set $a]]
    } else {
      lappend info [uplevel 2 [list subst $a]]
    }
  }
  tclLog "$loc  $info"
}

# look for files to add as optional modules (sourced on 1st use, not reloaded)
proc autoload {dir {repos ""}} {
  global auto_index
  set abbrevs {}
  foreach f [glob -nocomplain -dir $dir -tails *.tcl */*.tcl] {
    set name [file root $f]
    if {![info exists auto_index($name)]} {
      set path [file norm $dir/$f]
      #XXX notify rig.Defined $dir $name
      set auto_index($name) [list Rig loader $name $path]
      if {$name ne [file tail $name]} {
        lappend abbrevs [file tail $name]
      }
    }
  }
  if {$repos ne ""} {
    set url [string trimright $repos /]/0.x

    foreach f [uncomment [readhttp $url/all.def]] {
      set name [file root $f]
      if {![info exists auto_index($name)]} {
        set path [file norm $dir/$f]
        #XXX notify rig.Defined $url $name
        set auto_index($name) [list Rig loader $name $path $url/$f]
        lappend abbrevs [file tail $name]
      }
    }
  }
  foreach short $abbrevs {
    set match [array names auto_index */$short]
    if {[llength $match] == 1} {
      set auto_index($short) [list interp alias {} $short {} {*}$match]
    } else {
      array unset auto_index $short
    }
  }
}

# called via $auto_index when a module is first used, to set up as an ensemble
proc loader {name path {url ""}} {
  if {![file exists $path] && $url ne ""} {
    notify rig.Fetching $name $url
    writefile $path [readhttp $url]
  }
  namespace eval ::$name {
    namespace export -clear {[a-z]*}
    namespace ensemble create
  }
  namespace inscope ::$name source $path
  notify rig.Loaded $name
}

# special case when sourced for the first time, will load and setup all modules
ifndef ::Rig {
  namespace export -clear {[a-z]*}
  namespace ensemble create
  set v::lastreload 0
  set v::rigdir [file norm [file dir [info script]]]
  array set ::auto_index {
    bgerror { interp alias {} bgerror {} Rig bgerr  }
    Chk:    { interp alias {} Chk:    {} Rig check  }
    Log:    { interp alias {} Log:    {} Rig log    }
    Msg:    { interp alias {} Msg:    {} msgcat::mc }
    Tag:    { interp alias {} Tag:    {} Rig tag    }
  }
  #XXX notify rig.Started [date]
  # 1 catch, 2 if, 3 here, 4 outside ifndef, 5 outside namespace eval, 6 caller
  if {![catch { file norm [dict get [info frame -6] file] } v::mainscript]} {
    # full launch mode, do not start app loop in here, just load all the rigs
    reload
  } else {
    # not sourced from a main script but called directly
    set v::mainscript [file norm [info script]]
    Rig autoload [Rig home]/rigcache http://contrib.mavrig.org/
    namespace eval :: {
      if {[file exists [lindex $::argv 0]]} {
        # called with file, load just that one as rig
        set ::argv [lassign $::argv arg1]
        Rig loader [file root [file tail $arg1]] $arg1
      } else {
        # load all files next to Rig.tcl as rigs
        Rig reload
      }
      # since there is no main script, run the default app logic here
      Rig notify main.Init $argv
      Rig notify main.Run
      if {![info exists Rig::exit]} { vwait Rig::exit }
      Rig notify main.Done $Rig::exit
      exit $Rig::exit
    }
  }
}

} ;# namespace eval ::Rig

Rig doc "Template engine based on a Mason-like design" {
  version "$Id$"
}

# several procs for general component use are defined at the end of this file
namespace eval v {
  namespace export -clear * ;# all cmds will be imported into each request
  variable roots            ;# list of root dirs used to find components
  variable current          ;# currently active request object
}

# remember the root path(s) to use
proc setup {args} {
  set v::roots $args
}

# set up a fresh request object/namespace to dispatch it as a component
proc dispatch {obj} {
  variable ${obj}::request
  set request(chain) {}

  # make sure the request object has access to the definitions in v
  namespace inscope $obj namespace import [namespace current]::v::*
  
  # remember this object as the "current" one
  set v::current $obj

  set name $request(url)
  set tail {}
  set ns [Component $name]
  #FIXME - messy: name == "/" is found, but need to use default.mav
  # the underlying problem is: what should diff between /haha and /haha/ be?
  if {$ns eq "" || $name eq "/" || ![info exists $ns]} {
    # look for the first default handler upwards in the url path
    while {$name ne "/"} {
      if {[Component [file join $name default.mav]] ne ""} { break }
      set tail [linsert $tail 0 [file tail $name]]
      set name [file dir $name]
    }
    set ns [Component [file join $name default.mav]]
    if {$ns eq ""} {
      return -code 404 ;#FIXME looks like this never happens :(
    }
  }
  # construct the wrapping chain
  foreach ns [concat $ns [namespace inscope $ns namespace path]] {
    if {[llength [info commands ${ns}::.$request(proto)]]} {
      set request(chain) [linsert $request(chain) 0 $ns]
    }
  }
  set request(basens) [lindex $request(chain) end]
}

# locate a component in the site hierarchy, parse from disk as needed
proc Component {name} {
  set ns ""
  set cpath ""
  
  foreach s [file split $name] {
    append ns :: $s
    set cpath [string trim $cpath/$s /]
    #FIXME this is wasteful since the *absence* of files is not cached
    if {![namespace exists $ns]} {
      # find the last matching entry in roots
      foreach r $v::roots {
        set p [file join $r $cpath]
        if {[file exists $p]} {
          # set up the ns and default inheritance chain
          namespace eval $ns {}
          if {$ns eq "::/"} {
            # add v ns for commands which are available to all requests
            namespace inscope $ns namespace path [namespace current]::v
          } else {
            set parent [namespace parent $ns]
            set paths [namespace inscope $parent namespace path]
            namespace inscope $ns namespace path [linsert $paths 0 $parent]
          }
        }
        # remember file details in a variable with the same name as the ns
        if {[file isfile $p]} {
          set $ns [list time ? auto 0 path $p]
        } elseif {[file isfile $p/auto.mav]} {
          set $ns [list time ? auto 1 path $p/auto.mav]
        }
      }
      # at this point the ns exists if the path so far exists as dir or file
      if {![namespace exists $ns]} { return }
    }
    # the dict variable exists only if there is a file (or auto handler)
    if {[info exists $ns]} {
      dict with $ns {
        if {$time ne [file mtime $path]} {
          if {$time ne "?"} {
            # remove all procs and local variables but keep namespace children
            foreach _ [info commands ${ns}::*] { 
              rename $_ "" 
            }
            foreach _ [info vars ${ns}::*] {
              if {!$auto || ![namespace exists $_]} {
                unset -nocomplain $_
              }
            }
          }
          # finally, load the file and parse/process it
          set time [Parser $path $ns]
        }
      }
    }
  }
  return $ns
}

# parser for component files, i.e. a mix of rendering and script code
proc Parser {path ns} {
  namespace eval $ns {}
  variable ${ns}::dest   ;# current method being compiled
  variable ${ns}::argv   ;# array: key = method, value = arglist

  set dest .GET
  set argv($dest) ""
  set scripts($dest) ""
  set time [file mtime $path]

  set fd [open $path]
  set data [read $fd]
  close $fd

  set pos 0
  foreach pair [regexp -line -all -inline -indices {^<%|%>$} $data] {
    lassign $pair from to
    set s [string range $data $pos [expr {$from-1}]]
    switch [string range $data $from $to] {
      <% { append scripts($dest) [substify [string trimright $s]] \n }
      %> { namespace eval $ns $s; incr to }
    }
    set pos [incr to]
  }
  append scripts($dest) [substify [string range $data $pos end]]

  foreach {name body} [array get scripts] {
    proc ${ns}::$name $argv($name) "\n[namespace which InjectVars] $ns\n$body"
  }

  unset dest argv
  return $time
}

# use Tcl's substitution for the heavy-lifting, which it manages nicely
proc substify {in {var OUT}} {
  set pos -2  
  foreach pair [regexp -line -all -inline -indices {^%[\s#].*$} $in] {
    lassign $pair from to
    set s [string range $in $pos+2 $from-2]
    if {$s ne ""} {
      append script {append } $var { [subst } [list $s] {]} \n
    }
    append script [string range $in $from+1 $to] "\n"
    set pos $to
  }
  set s [string range $in $pos+2 end]
  return [append script {append } $var { [subst } [list $s] {]} \n]
}

# insert all currently defined veriable as locals when a method runs
proc InjectVars {ns} {
  foreach x [info vars ${ns}::*] {
    uplevel upvar $x [namespace tail $x]
  }
}

# start the definition of a new method in the current component
proc v::DEF {name args} {
  set ns [uplevel { namespace current }]
  set ${ns}::argv($name) $args
  set ${ns}::dest $name
}

# component method call
proc v::CALL {name args} {
  variable current
  variable ${current}::request
  uplevel namespace inscope $request(basens) $name $args
}

# nesting hierarchy expansion, this supports embedded sub-components
proc v::NEXT {args} {
  variable current
  variable ${current}::request
  if {[llength $request(chain)] > 0} {
    set request(chain) [lassign $request(chain) ns]
    ${ns}::.$request(proto) {*}$args
  } else {
    return -code 404
  }
}

# inquire about or change the parent chain path of a component
proc v::INHERIT {{path .}} {
  switch -- $path {
    .       { uplevel namespace path }
    ""      { uplevel [list namespace path [namespace current]] }
    default { error ;# FIXME notyet }
  }
}

# utility and accessor for the current request object
proc v::REQ {{key ""} args} {
  variable current
  switch $key {
    "" {
      return $current
    }
    H {
      variable ${current}::headers
      switch [llength $args] {
        0 { return [dict keys $headers] }
        1 { return [dict get $headers {*}$args] }
        default { dict set headers {*}$args }
      }
    }
    I {
      variable ${current}::info
      if {[llength $args]} {
        set args [lassign $args key]
        return [set info([string tolower $key]) {*}$args]
      } else {
        return [array names info]
      }
    }
    Q {
      variable ${current}::query
      if {[llength $args] == 0} {
        return $query
      }
      variable ${current}::request
      if {![info exists request(args)]} {
        set request(args) {}
        # remove trailing newline (in case query came as POST data)
        foreach x [split [string trim $query] &] {
          lassign [split $x =] param value
          lappend request(args) $param [Web urldecode $value]
        }
      }
      lassign $args param value
      if {[dict exists $request(args) $param]} {
        set value [dict get $request(args) $param]
      }
      return $value
    }
    default {
      return [set ${current}::request($key) {*}$args]
    }
  }
}

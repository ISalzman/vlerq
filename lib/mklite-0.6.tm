# Compatibility layer for Metakit
# $Id$
#
# 0.3 modified to use the vlerq extension i.s.o. thrive
# 0.4 adjusted for vlerq 4
# 0.5 support "mk::file open" (no other args) and more mk::select options
# 0.6 adjusted for rig views
#
# View subcommands used in this code: open load get rowmap colmap sort

package provide mklite 0.6

namespace eval mklite {
  variable mkdb           ;# array, maps open db handles to view references
  variable mktype readkit ;# view type to use in this code

  # call emulateMk4tcl to define compatibility aliases in a specific namespace
  proc emulateMk4tcl {{ns ::mk}} {
    foreach x {channel cursor file get loop select view} {
      # NOTE: clobbers existing mk::* cmds, may want to rename 'em first?
      interp alias {} ${ns}::$x {} [namespace current]::$x
    }
  }

# proc debug {args} {
#   puts ">>> [list $args]"
#   set r [uplevel 1 $args]
#   set s [regsub -all {[^ -~]} $r ?]
#   puts " << [string length $r]: [string range $s 0 49]"
#   return $r
# }

  proc mk_obj {path} {
    variable mkdb
    # memoize last result (without row number), it's likely to be used again
    set pre [regsub {!\d+$} $path {}]
    if {$mkdb(?) eq $pre} { return $mkdb(:) }
    set n 0
    foreach {x y} [regsub -all {[:.!]} :$path { & }] {
      switch -- $x {
        : { set r $mkdb($y) }
        ! { set n $y }
        . { if {[catch { View get $r $n $y } r]} { return 0 } }
      }
    }
    set mkdb(?) $pre
    set mkdb(:) $r
    return $r
  }

  # partial emulation of the mk::* commands

  proc file {cmd args} {
    variable mkdb
    variable mkpath
    variable mktype
    set db [lindex $args 0]
    if {$db eq ""} { return [array get mkpath] }
    set file [lindex $args 1]
    switch $cmd {
      open {
        set mkdb(?) ""
        if {$file ne ""} {
          set mkdb($db) [View open $mktype $file]
        } else {
          set mkdb($db) 0
        }
        set mkpath($db) $file
        return $db 
      }
      load {
        fconfigure $file -translation binary
        set mkdb($db) [View load $mktype [read $file]]
        return $db 
      }
      close {
        set mkdb(?) ""
        unset mkdb($db) mkpath($db)
        return 
      }
      views {
        return [View get $mkdb($db) @ * 0]
      }
    }
    error "mkfile $cmd?"
  }

  proc view {cmd path args} {
    set a1 [lindex $args 0]
    switch $cmd {
      info {
        return [View get [mk_obj $path] @ * 0]
      }
      layout {
        set layout "NOTYET"
        if {[llength $args] > 0 && $layout != $a1} {
          #error "view restructuring not supported"
        }
        return $layout
      }
      size {
        set len [View get [mk_obj $path] #]
        if {[llength $args] > 0 && $len != $a1} {
          error "view resizing not supported"
        }
        return $len
      }
      default { error "mkview $cmd?" }
    }
  }

  proc cursor {cmd cursor args} {
    upvar $cursor v
    switch $cmd {
      create {
        NOTYET
      }
      incr {
        NOTYET
      }
      pos -
      position {
        if {$args != ""} {
          unset v
          regsub {!-?\d+$} $v {} v
          append v !$args
          return $args
        }
        if {![regexp {\d+$} $v n]} {
          set n -1
        }
        return $n
      }
      default { error "mkcursor $cmd?" }
    }
  }

  proc get {path args} {
    set vw [mk_obj $path]
    set row [regsub {^.*!} $path {}]
    set sized 0
    if {[lindex $args 0] eq "-size"} {
      incr sized
      set args [lrange $args 1 end]
    }
    set ids 0
    if {[llength $args] == 0} {
      incr ids
      set args [View get $vw @ * 0]
    }
    set r {}
    foreach x $args {
      if {$ids} { lappend r $x }
      set v [View get $vw $row $x]
      if {$sized} { set v [string length $v] }
      lappend r $v
    }
    if {[llength $args] == 1} { set r [lindex $r 0] }
    return $r
  }

  proc loop {cursor path args} {
    upvar $cursor v
    #unset -nocomplain v ;# avoids errors if v already exists as array
    if {[llength $args] == 0} {
      set args [list $path]
      set path $v
      regsub {!-?\d+$} $path {} path
    }
    set first [lindex $args 0]
    set last [lindex $args 1]
    switch [llength $args] {
      1 { set first 0
          set limit [View get [mk_obj $path] #]
          set step 1 }
      2 { set limit [View get [mk_obj $path] #]
          set step 1 }
      3 { set step 1 }
      4 { set step [lindex $args 2] }
      default { error "mkloop arg count?" }
    }
    set body [lindex $args end]
    set code 0
    for {set i $first} {$i < $limit} {incr i $step} {
      set v $path!$i
      set code [catch [list uplevel 1 $body] err]
      switch $code {
        1 -
        2 { return -code $code $err }
        3 { break }
      }
    }
  }

  proc select {path args} {
    set vw [mk_obj $path]
    set n [View get $vw #]
    set r {}
    for {set i 0} {$i < $n} {incr i} { lappend r $i }
    set n [llength $args]
    set sorts {}
    for {set i 0} {$i < $n} {incr i} {
      set match std
      switch -- [lindex $args $i] {
        -sort    { lappend sorts [lindex $args [incr i]]; continue }
        -rsort   { lappend sorts - [lindex $args [incr i]]; continue }
        -first   { set first [lindex $args [incr i]]; continue }
        -count   { set count [lindex $args [incr i]]; continue }
        -glob    { set match glob; incr i }
        -globnc  { set match globnc; incr i }
        -keyword { set match keyword; incr i }
      }
      set cols [lindex $args $i]
      set k [lindex $args [incr i]]
      set kwre "\\m$k"
      # could use loop -collect, might be faster
      set r2 {}
      foreach x $r {
        foreach c $cols {
          set v [View get $vw $x $c]
          switch $match {
            glob    { set ok [string match $k $v] }
            globnc  { set ok [string match -nocase $k $v] }
            keyword { set ok [regexp -nocase $kwre $v] }
            default { set ok [expr {$k eq $v}] }
          }
          if {$ok} { lappend r2 $x; break }
        }
      }
      set r $r2
    }
    if {[llength $sorts]} {
      foreach x [lreverse $sorts] {
        if {$x eq "-"} {
          set r [lreverse $r]
        } else {
          set v [View rowmap [View tag [View colmap $vw $x] N] $r]
          set r [View get [View sort $v] * N]
        }
      }
    }
    if {[info exists first]} {
      set r [lrange $r $first end]
    }
    if {[info exists count]} {
      set r [lrange $r 0 [incr count -1]]
    }
    return $r
  }

  proc channel {path name mode} {
    package require vfs ;# TODO: needs vfs, could use "chan create" in 8.5
    if {$mode ne "r"} { error "mkchannel? mode $mode" }
    set fd [::vfs::memchan]
    fconfigure $fd -translation binary
    puts -nonewline $fd [get $path $name]
    seek $fd 0
    return $fd
  }
}

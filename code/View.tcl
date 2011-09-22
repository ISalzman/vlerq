# High-level data view module

# dispatch "View op v ..." to "View::${type}::^op ..." or else "View::^op ..."
proc Unknown {cmd op v args} {
  # resolve right now iso returning ns inscope to avoid inserting extra level
  set ns [namespace current]::[lindex $v 0]
  return [namespace inscope $ns namespace which ^$op]
}
namespace ensemble create -unknown [namespace code Unknown]

# create a new ref view with the specified size and structure
proc new {rows {meta {ref {}}}} {
  return [View wrap ref [View init data $rows $meta]]
}

# define a view from scratch
proc def {desc {data ""}} {
  set v [View new 0 [desc2meta $desc]]
  View append $v {*}$data
  return $v
}

# true if the view passed in is a meta view
proc ismeta? {v} {
  return [expr {[View meta $v] eq "ref {}"}]
}

# convert a Mk4tcl layout string to a meta-view
proc layout2meta {layout} {
  set meta [View new 0]
  set empty [View deref $meta]
  foreach d $layout {
    if {[llength $d] > 1} {
      lassign $d name subv
      View append $meta $name V [layout2meta $subv]
    } else {
      lassign [split ${d}:S :] name type
      View append $meta $name $type $empty
    }
  }
  return [View destroy $meta] ;# get rid of the modifiable wrapper
}

# convert a Metakit description string to a meta-view
proc desc2meta {d} {
  # adapted from readkit.tcl, converts a Metakit description to a Mk4tcl layout
  set l [string map {] "\}\}" , " "} [regsub -all {(\w+)\s*\[} $d "\{\\1 \{"]]
  return [layout2meta $l]
}

# loop to iterate over items in a view and collect the results
proc loop {v ivar vars body} {
  upvar 1 $ivar i
  foreach x $vars {
    upvar 1 $x _$x
  }
  set o {}
  set n [View size $v]
  for {set i 0} {$i <$n} {incr i} {
    set c -1
    foreach x $vars {
      set _$x [View at $v $i [incr c]]
    }
    set e [catch [list uplevel 1 $body] r]
    switch $e {
      0 - 2 {}
      1     { return -code $e $r }
      3     { return $o }
      4     { continue }
    }
    lappend o $r
  }
  return $o
}

proc OneCol {v {col 0}} {
  set n [View size $v]
  set o {}
  for {set r 0} {$r < $n} {incr r} {
    lappend o [View at $v $r $col]
  }
  return $o
}

proc OneRow {v row {named ""}} {
  if {$named ne ""} {
    set m [OneCol [View meta $v] 0]
  }
  set n [View size [View meta $v]]
  set o {}
  for {set c 0} {$c < $n} {incr c} {
    if {$named ne ""} {
      lappend o [lindex $m $c]
    }
    lappend o [View at $v $row $c]
  }
  return $o
}

# lookup column names
proc ColNum {v argv} {
  set names [OneCol [View meta $v] 0]
  set r {}
  foreach x $argv {
    if {![string is integer -strict $x]} {
      set y [lsearch -exact $names $x]
      if {$y >= 0} {
        set x $y
      }
    }
    lappend r $x
  }
  return $r
}

# general access operator
proc get {v args} {
  if {[llength $args] == 0} {
    lappend args * *
  }
  while {[llength $args] > 0} {
    set args [lassign $args row col]
    switch $row {
      \# {
        return [View size $v]
      }
      @ {
        set v [View meta $v ]
        if {$col eq ""} { return $v }
        set args [linsert $args 0 $col]
        continue
      }
      * {
        switch $col {
          * { return [concat {*}[View loop $v i {} { OneRow $v $i }]] }
          "" { return [View loop $v i {} { OneRow $v $i }] }
          default { return [OneCol $v [ColNum $v $col]] }
        }
      }
      default {
        switch $col {
          * { return [OneRow $v $row] }
          "" { return [OneRow $v $row -named] }
          default { set v [View at $v $row [ColNum $v $col]] }
        }
      }
    }
  }
  return $v
}

# default operators are found via "namespace path"
proc ^deref {v} {
  return $v
}
proc ^destroy {v} {
  # nothing
}
proc ^meta {v} {
  return [lindex $v 1]
}
proc ^size {v} {
  return [lindex $v 2]
}

# "data" meta size col0 col1 ...
namespace eval data {
  namespace path [namespace parent]
  
  proc ^init {- rows meta} {
    set ncols [View size $meta]
    set v [list data [View deref $meta] $rows]
    for {set c 0} {$c < $ncols} {incr c} {
      if {$rows == 0} {
        lappend v {}
      } else {
        switch [View at $meta $c 1] {
          I - L - F - D { set zero 0 }
          V             { set zero [View init data 0 [View at $meta $c 2]] }
          default       { set zero "" }
        }
        lappend v [lrepeat $rows zero]
      }
    }
    return $v
  }

  proc ^at {v row col} {
    return [lindex $v $col+3 $row]
  }
  
  proc ^set/nd {v row col value} {
    return [lset v $col+3 $row $value]
  }
  proc ^replace/nd {v row count w} {
    set last [expr {$row+$count-1}]
    set cols [View size [View meta $v]]
    for {set c 0} {$c < $cols} {incr c} {
      lset v $c+3 [lreplace [lindex $v $c+3] $row $last {*}[OneCol $w $c]]
    }
    return [lset v 2 [expr {[View size $v] - $count + [View size $w]}]]
  }
  proc ^append/nd {v args} {
    set cols [View size [View meta $v]]
    # Rig check {$cols != 0}
    # Rig check {[llength $args] % $cols == 0}
    set i -1
    foreach value $args {
      set offset [expr {[incr i] % $cols + 3}]
      lset v $offset [linsert [lindex $v $offset] end $value]
    }
    return [lset v 2 [llength [lindex $v 3]]]
  }
}

# "ref" handle ?r1 c1 r2 c2 ...?
namespace eval ref {
  namespace path [namespace parent]
  
  namespace eval v {
    variable seq  ;# sequence number to issue new handle names
    variable vwh  ;# array of r/w handles, key is handle, value is real view

    # pre-define the meta-meta view, i.e. {ref {}}
    set vwh() [list data {ref {}} 3 \
                {name type subv} {S S V} [lrepeat 3 {data {ref {}} 0 {} {} {}}]]
  }

  # turn a view into a handle-based modifiable type
  proc ^wrap {- v} {
    set h "v[incr v::seq]ref"
    set v::vwh($h) [View deref $v]
    return [list ref $h]
  }
  
  proc Descend {v} {
    set w $v::vwh([lindex $v 1])
    foreach {r c} [lrange $v 2 end] {
      set w [View at $w $r $c]
    }
    return $w
  }
  
  proc ^deref {v} {
    if {[lindex $v 1] eq ""} {
      return $v ;# can't deref, this is a recursive definition
    }
    return [Descend $v]
  }
  proc ^destroy {v} {
    set oref $v::vwh([lindex $v 1])
    unset v::vwh([lindex $v 1])
    View destroy $oref
    return $oref
  }

  proc ^meta {v} {
    return [View meta [Descend $v]]
  }
  proc ^size {v} {
    return [View size [Descend $v]]
  }
  proc ^at {v row col} {
    set m [View meta $v]
    lappend v $row $col
    # extra test to guard against infinite loop when v is a meta-view
    if {[lindex $v 1] eq "" || [View at $m $col 1] ne "V"} {
      set v [Descend $v]
    }
    return $v
  }
  
  # descend into proper subview, apply the non-destructive change, and unwind
  proc SubChange {op v argv} {
    set value [View $op [Descend $v] {*}$argv]
    while {[llength $v] > 2} {
      set row [lindex $v end-1]
      set col [lindex $v end]
      set v [lrange $v 0 end-2]
      set value [View set/nd [Descend $v] $row $col $value]
    }
    return $value
  }
  proc ^set/nd {v args} {
    return [SubChange set/nd $v $args]
  }
  proc ^set {v args} {
    set v::vwh([lindex $v 1]) [View set/nd $v {*}$args]
  }
  proc ^replace/nd {v args} {
    return [SubChange replace/nd $v $args]
  }
  proc ^replace {v args} {
    set v::vwh([lindex $v 1]) [View replace/nd $v {*}$args]
  }
  proc ^append/nd {v args} {
    return [SubChange append/nd $v $args]
  }
  proc ^append {v args} {
    set v::vwh([lindex $v 1]) [View append/nd $v {*}$args]
  }
}

# "deriv" meta size {op args...} ?...?
namespace eval deriv {
  namespace path [namespace parent]
  
  # child namespace for all getters, these are dispatched to by ^at
  namespace eval getter {}

  # this proc uses introspection to reconstruct the caller's original command
  # must be called with meta-view, size, and any further args to pass to getter
  proc New {meta size args} {
    set cmd [info level -1]
    lset cmd 0 [namespace tail [lindex $cmd 0]]
    return [linsert $args 0 deriv $meta $size $cmd]
  }

  proc ^at {v row col} {
    return [namespace inscope getter [list [lindex $v 3 0] $row $col {*}$v]]
  }
}

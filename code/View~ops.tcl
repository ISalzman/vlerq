# Additional view operators

# return a pretty text representation of a view, without nesting
proc dump {v {maxrows 20}} {
  set n [View size $v]
  if {[View width $v] == 0} { return "  ($n rows, no columns)" }
  set i -1
  foreach x [View names $v] y [View types $v] {
    if {$x eq ""} { set x ? }
    set v2 [View get $v * [incr i]]
    switch $y {
      B       { set s { "[string length $r]b" } }
      V       { set s { "#[View size $r]" } }
      default { set s { } }
    }
    set c {}
    foreach r $v2 {
      lappend c [eval set r $s]
    }
    set c [lrange $c 0 $maxrows-1]
    set w [string length $x]
    foreach z $c {
      if {[string length $z] > $w} { set w [string length $z] }
    }
    if {$w > 50} { set w 50 }
    switch $y {
      B - I - L - F - D - V   { append fmt "  " %${w}s }
      default                 { append fmt "  " %-$w.${w}s }
    }
    append hdr "  " [format %-${w}s $x]
    append bar "  " [string repeat - $w]
    lappend d $c
  }
  set r [list $hdr $bar]
  for {set i 0} {$i < $n} {incr i} {
    if {$i >= $maxrows} break
    set cmd [list format $fmt]
    foreach x $d { lappend cmd [regsub -all {[^ -~]} [lindex $x $i] .] }
    lappend r [eval $cmd]
  }
  if {$i < $n} { lappend r [string map {- .} $bar] }
  ::join $r \n
}

# return a pretty html representation of a view, including nesting
proc html {v {styled 1}} {
  set names [View names $v]
  set types [View types $v]
  set o <table>
  if {$styled} {
    append o {<style type="text/css"><!--\
      table { font: 8.5pt Verdana; }\
      tt table, pre table { border-spacing: 0; border: 1px solid #aaaaaa; }\
      th { background-color: #eeeeee; font-weight: normal; }\
      td { vertical-align: top; }\
      th, td { padding: 0 4px 2px 0; }\
      th.row,td.row { color: #cccccc; font-size: 80%; }\
    --></style>}
  }
  append o \n {<tr><th class="row"></th>}
  foreach x $names {
    if {$x eq ""} { set x ? }
    append o <th><i> $x </i></th>
  }
  append o </tr>\n
  set n [View size $v]
  for {set r 0} {$r < $n} {incr r} {
      append o {<tr><td align="right" class="row">} $r </td>
      set i -1
      foreach x $names y $types val [View get $v $r *] {
          switch $y {
              b - B   { set z [string length $val]b }
              v - V   { set z \n[View html $val 0]\n }
              default { 
                  set z [string map {& &amp\; < &lt\; > &gt\;} $val] 
              }
          }
          switch $y {
              s - S - v - V { append o {<td>} }
              default { append o {<td align="right">} }
          }
          append o $z </td>
      }
      append o </tr>\n
  }
  append o </table>
}

# define a view with one int column
proc ints {data} {
  #TODO optional arg flags column name lookup mode, to convert any column names
  return [View def :I $data]
}

# shorthand to get the number of columns
proc width {v} {
  return [View size [View meta $v]]
}

# shorthand to get the list of names
proc names {v} {
  return [View get $v @ * 0]
}

# shorthand to get the list of types
proc types {v} {
  return [View get $v @ * 1]
}

# return a structure description without column names
proc structure {v} {
  if {![ismeta? $v]} {
    set v [View meta $v]
  }
  set desc [View loop $v - {x type subv} {
    if {$type eq "V"} {
      set s [View structure $subv]
      if {$s ne ""} {
        return "($s)"
      }
    }
    return $type
  }]
  return [join $desc ""]
}

# map rows of v using col 0 of w
proc rowmap {v w} {
  return [deriv::New [View meta $v] [View size $w]]
}
proc deriv::getter::rowmap {row col - meta size cmd} {
  set v [lindex $cmd 1]
  set r [View at [lindex $cmd 2] $row 0]
  if {$r < 0} {
    set r [View at [lindex $cmd 2] $row+$r 0]
  }
  return [View at $v [expr {$r % [View size $v]}] $col]
}

# map columns of v using col 0 of w
proc colmap {v w} {
  return [deriv::New [View rowmap [View meta $v] $w] [View size $v]]
}
proc deriv::getter::colmap {row col - meta size cmd} {
  return [View at [lindex $cmd 1] $row [View at [lindex $cmd 2] $col 0]]
}

# cancatenate any number of views, which must all have the same structure
#TODO verify structure compat (maybe also "narrow down", similar to pair op?)
proc plus {v args} {
  set counts [View size $v]
  foreach a $args {
    lappend counts [View size $a]
  }
  return [deriv::New [View meta $v] [tcl::mathop::+ {*}$counts] $counts]
}
proc deriv::getter::plus {row col - meta size cmd counts} {
  set i 0
  foreach n $counts {
    incr i
    if {$row < $n} {
      return [View at [lindex $cmd $i] $row $col]
    }
    set row [expr {$row - $n}]
  }
}

# put views "next" to each other, result size is size of smallest one
proc pair {v args} {
  set size [View size $v]
  set metas [list [View meta $v]]
  set widths [View width $v]
  foreach a $args {
    set size [expr {min($size,[View size $a])}]
    set m [View meta $a]
    lappend metas $m
    lappend widths [View size $m]
  }
  return [deriv::New [View plus {*}$metas] $size $widths]
}
proc deriv::getter::pair {row col - meta size cmd widths} {
  set i 0
  foreach n $widths {
    incr i
    if {$col < $n} {
      return [View at [lindex $cmd $i] $row $col]
    }
    set col [expr {$col - $n}]
  }
}

# iota and step generator
proc step {count {off 0} {step 1} {rate 1}} {
  return [deriv::New [desc2meta :I] $count $off $step $rate]
}
proc deriv::getter::step {row col - meta size cmd off step rate} {
  return [expr {$off + $step * ($row / $rate)}]
}

# reverse the row order
proc reverse {v} {
  set rows [View size $v]
  return [View rowmap $v [View step $rows [expr {$rows-1}] -1]]
}

# return the first n rows
proc first {v n} {
  return [View pair $v [View new $n]]
}

# return the last n rows
proc last {v n} {
  return [View reverse [View first [View reverse $v] $n]]
}

# repeat each row x times
proc spread {v x} {
  set n [View size $v]
  return [View rowmap $v [View step [expr {$n * $x}] 0 1 $x]]
}

# repeat the entire view x times
proc times {v x} {
  return [View rowmap $v [View step [expr {[View size $v] * $x}]]]
}

# cartesian product
proc product {v w} {
  return [View pair [View spread $v [View size $w]] \
                    [View times $w [View size $v]]]
}

# return a set of ints except those listed in the map
proc omitmap {v n} {
  set o {}
  foreach x [View get $v * 0] {
    dict set o $x ""
  }
  set m {}
  for {set i 0} {$i < $n} {incr i} {
    if {![dict exists $o $i]} {
      lappend m $i
    }
  }
  return [View ints $m]
}

# omit the specified rows from the view
proc omit {v w} {
  return [View rowmap $v [View omitmap $w [View size $v]]]
}

# a map with indices of only the first unique rows
proc uniqmap {v} {
  set m {}
  set n [View size $v]
  for {set i 0} {$i < $n} {incr i} {
    set r [View get $v $i *]
    if {![dict exists $m $r]} {
      dict set m $r $i
    }
  }
  return [View ints [dict values $m]]
}

# return only the unique rows
proc unique {v} {
  return [View rowmap $v [View uniqmap $v]]
}

# relational projection
proc project {v cols} {
  return [View unique [View colmap $v [View ints [ColNum $v $cols]]]]
}

# create a view with same structure but no content
proc clone {v} {
  return [View new 0 [View meta $v]]
}

# collect indices of identical rows
proc RowGroups {v} {
  set m {}
  set n [View size $v]
  for {set i 0} {$i < $n} {incr i} {
    dict lappend m [View get $v $i *] $i
  }
  return $m
}

# indices of all rows in v which are also in w
proc isectmap {v w} {
  set g [RowGroups $w]
  set m {}
  set n [View size $v]
  for {set i 0} {$i < $n} {incr i} {
    set r [View get $v $i *]
    if {[dict exists $g $r]} {
      lappend m $i
    }
  }
  return [View ints $m]
}

# set intersection
proc intersect {v w} {
  return [View rowmap $v [View isectmap $v $w]]
}

# indices of all rows in v which are not in w
proc exceptmap {v w} {
  return [View omitmap [View isectmap $v $w] [View size $v]]
}

# set exception
proc except {v w} {
  return [View rowmap $v [View exceptmap $v $w]]
}

# set union
proc union {v w} {
  return [View plus $v [View except $w $v]]
}

# return a groupmap
proc groupmap {v {name ""} {desc ""} {v2 ""} {ovar ""}} {
  append desc :I
  set g [RowGroups $v]
  set m [View new 0]
  View append $m $name V [desc2meta $desc]
  set w [View new 0 $m]
  set i -1
  dict for {x y} $g {
    View append $w [View def $desc $y]
    dict set g $x [incr i]
  }
  if {$ovar ne ""} {
    set o {}
    set e [View size $v]
    set n [View size $v2]
    for {set i 0} {$i < $n} {incr i} {
      set r [View get $v2 $i *]
      if {[dict exists $g $r]} {
        lappend o [dict get $g $r]
      } else {
        lappend o $e
      }
    }
    # return the map view with v2 lookups
    uplevel [list set $ovar [View ints $o]]
    # add an empty subview for rows in v2 which are not in v
    View append $w [View def $desc {}]
  }
  return $w
}

# return a grouped view
proc group {v cols {name ""}} {
  set cmap [View ints [ColNum $v $cols]]
  set keys [View colmap $v $cmap]
  set rest [View colmap $v [View omitmap $cmap [View width $v]]]
  set gmap [groupmap $keys]
  set head [View ints [View loop $gmap - x { View at $x 0 0 }]]
  set meta [View new 0]
  View append $meta $name V [View meta $rest]
  set nest [deriv::New $meta [View size $gmap] $rest $gmap]
  return [View pair [View rowmap $keys $head] $nest]
}
proc deriv::getter::group {row col - meta size cmd rest gmap} {
  return [View rowmap $rest [View at $gmap $row 0]]
}

# ungroup a view on specified column
proc ungroup {v col} {
  set col [ColNum $v $col]
  set m {}
  set n [View size $v]
  for {set i 0} {$i < $n} {incr i} {
    set k [View size [View at $v $i $col]]
    if {$k > 0} {
      lappend m $i
      for {set j 1} {$j < $k} {incr j} {
        lappend m -$j
      }
    }
  }
  set smap [View ints $m]
  set rest [View colmap $v [View omitmap [View ints $col] [View width $v]]]
  set subs [View colmap $v [View ints $col]]
  set meta [View at [View meta $subs] 0 2]
  set flat [deriv::New $meta [View size $smap] $smap $subs]
  return [View pair [View rowmap $rest $smap] $flat]
}
proc deriv::getter::ungroup {row col - meta size cmd smap subs} {
  set pos [View at $smap $row 0]
  if {$pos >= 0} {
    set off 0
  } else {
    set off [expr {-$pos}]
    set pos [View at $smap [expr {$row + $pos}] 0]
  }
  return [View at [View at $subs $pos 0] $off $col]
}

# join operator
proc ^join {v w {name ""}} {
  #TODO don't compare subview columns for now
  set vn [View colmap [View meta $v] [View ints {0 1}]]
  set wn [View colmap [View meta $w] [View ints {0 1}]]
  set vi [View isectmap $vn $wn]
  set wi [View isectmap $wn $vn]
  set vkeys [View colmap $v $vi]
  set wkeys [View colmap $w $wi]
  #set vrest [View colmap $v [View omitmap $vi [View size $vn]]]
  set wrest [View colmap $w [View omitmap $wi [View size $wn]]]
  set gmap [View groupmap $wkeys $name "" $vkeys omap]
  set meta [View new 0]
  View append $meta $name V [View meta $wrest]
  set nest [deriv::New $meta [View size $omap] $wrest $gmap $omap]
  return [View pair $v $nest]
}
proc deriv::getter::^join {row col - meta size cmd rest gmap omap} {
  return [View rowmap $rest [View at $gmap [View at $omap $row 0] 0]]
}

# inner join
proc ijoin {v w} {
  set x [View join $v $w]
  return [View ungroup $x [expr {[View width $x] - 1}]]
}

# make a shallow copy, return as new var view
proc copy {v} {
  set o [View new 0 [View meta $v]]
  set n [View size $v]
  for {set i 0} {$i < $n} {incr i} {
    View append $o {*}[View get $v $i *]
  }
  return $o
}

# rename columns
proc ^rename {v args} {
  #set m [View wrap mut [View meta $v]] ;# uses a mutable view
  set m [View copy [View meta $v]] ;# avoids dependency on View~mut
  foreach {x y} $args {
    View set $m [ColNum $v $x] 0 $y
  }
  return [deriv::New $m [View size $v]]
}
proc deriv::getter::^rename {row col - meta size cmd} {
  return [View at [lindex $cmd 1] $row $col]
}

# blocked views
proc blocked {v} {
  #Rig check {[View width $v] == 1}
  set tally 0
  set offsets {}
  set rows [View size $v]
  for {set i 0} {$i < $rows} {incr i} {
    lappend offsets [incr tally [View size [View at $v $i 0]]]
  }
  set meta [View at [View meta $v] 0 2]
  return [deriv::New $meta $tally $offsets]
}
proc deriv::getter::blocked {row col - meta size cmd offsets} {
  set v [lindex $cmd 1]
  set block -1
  foreach o $offsets {
    if {[incr block] + $o >= $row} break
  }
  if {$row == $block + $o} {
    set row $block
    set block [expr {[View size $v] - 1}]
  } elseif {$block > 0} {
    set row [expr {$row - $block - [lindex $offsets $block-1]}]
  }
  return [View at [View at $v $block 0] $row $col]
}

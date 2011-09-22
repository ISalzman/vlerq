# View adapter for readkit, a Metakit reader in Tcl (included)

# "readkit" meta path
namespace eval readkit {
  namespace path [namespace parent]
  
  variable seq  ;# used to generate db names
  
  # open a datafile and return a storage view, has 1 row with all views on file
  proc ^open {- args} {
    variable seq
    set db "db[incr seq]readkit"
    Readkit dbopen $db {*}$args
    set m [View layout2meta [Readkit dblayout $db]]
    set v [View new 1 $m]
    set n [View size $m]
    for {set i 0} {$i < $n} {incr i} {
      View set $v 0 $i [list readkit [View at $m $i 2] $db.[View at $m $i 0]]
    }
    return $v
  }
  
  proc ^size {v} {
    return [Readkit vlen [Readkit access [lindex $v 2]]]
  }
  proc ^at {v row col} {
    set name [View at [lindex $v 1] $col 0]
    if {[lindex $v 1] ne "" && [View at [lindex $v 1] $col 1] eq "V"} {
      return [list readkit [View at [lindex $v 1] $col 2] \
                                    [lindex $v 2]!$row.$name]
    }
    return [Readkit::Mvec [Readkit access [lindex $v 2]!$row] $name]
  }
}

# Metakit datafile reader in Tcl
namespace eval readkit::Readkit {
  namespace export -create {[a-z]*}
  namespace ensemble create

  # Mmap and Mvec primitives in pure Tcl (a C version is present in critlib)

  namespace eval v {
    array set mmap_data {}
    array set mvec_shifts {
      - -1    0 -1
      1  0    2  1    4  2    8   3
      16 4   16r 4
      32 5   32r 5   32f 5   32fr 5
      64 6   64r 6   64f 6   64fr 6
    }
  }

  proc Mmap {fd args} {
    upvar 0 v::mmap_data($fd) data
    # special case if fd is the name of a variable (qualified or global)
    if {[uplevel #0 [list info exists $fd]]} {
      upvar #0 $fd var
      set data $var
    }
    # cache a full copy of the file to simulate memory mapping
    if {![info exists data]} {
      set pos [tell $fd]
      seek $fd 0 end
      set end [tell $fd]
      seek $fd 0
      set trans [fconfigure $fd -translation]
      fconfigure $fd -translation binary
      set data [read $fd $end]
      fconfigure $fd -translation $trans
      seek $fd $pos
    }
    set total [string length $data]
    if {[llength $args] == 0} {
      return $total
    }
    lassign $args off len
    if {$len < 0} {
      set len $total
    }
    if {$len < 0 || $len > $total - $off} {
      set len [expr {$total - $off}]
    }
    binary scan $data @${off}a$len s
    return $s
  }

  proc Mvec {v args} {
    lassign $v mode data off len
    if {[info exists v::mvec_shifts($mode)]} {
      # use MvecGet to access elements
      set shift $v::mvec_shifts($mode)
      if {[llength $v] < 4} {
        set len $off
      }
      set get [list MvecGet $shift $v *]
    } else {
      # virtual mode, set to evaluate script
      set shift ""
      set len [lindex $v end]
      set get $v
    }
    # try to derive vector length from data length if not specified
    if {$len eq "" || $len < 0} {
      set len 0
      if {$shift >= 0} {
        if {[llength $v] < 4} {
          set n [string length $data] 
        } else {
          set n [Mmap $data]
        }
        set len [expr {($n << 3) >> $shift}]
      }
    }
    set nargs [llength $args]
    # with just a varname as arg, return info about this vector
    if {$nargs == 0} {
      if {$shift eq ""} {
        return [list $len {} $v]
      }
      return [list $len $mode $shift]
    }
    lassign $args pos count pred cond
    # with an index as second arg, do a single access and return element
    if {$nargs == 1} {
      return [uplevel 1 [lreplace $get end end $pos]]
    }
    if {$count < 0} {
      set count $len
    }
    if {$count > $len - $pos && $shift != -1} {
      set count [expr {$len - $pos}]
    }
    if {$nargs == 4} {
      upvar $pred x
    }
    set r {}
    incr count $pos
    # loop through specified range to build result vector
    # with four args, used that as predicate function to filter
    # with five args, use fourth as loop var and apply fifth as condition
    for {set x $pos} {$x < $count} {incr x} {
      set y [uplevel 1 [lreplace $get end end $x]]
      switch $nargs {
        3 { if {![uplevel 1 [list $pred $v $x $y]]} continue }
        4 { if {![uplevel 1 [list expr $cond]]} continue }
      }
      lappend r $y
    }
    return $r
  }

  proc MvecGet {shift desc index} {
    lassign $desc mode data off len
    switch -- $mode {
      - { return $index }
      0 { return $data }
    }
    if {[llength $desc] < 4} {
      set off [expr {($index << $shift) >> 3}] 
    } else {
      # don't load more than 8 bytes from the proper offset
      incr off [expr {($index << $shift) >> 3}]
      set data [Mmap $data $off 8]
      set off 0
    }
    switch -- $mode {
      1 {
        binary scan $data @${off}c value
        return [expr {($value>>($index&7))&1}]
      }
      2 {
        binary scan $data @${off}c value
        return [expr {($value>>(($index&3)<<1))&3}]
      }
      4 {
        binary scan $data @${off}c value
        return [expr {($value>>(($index&1)<<2))&15}]
      }
      8    { set w 1; set f c }
      16   { set w 2; set f s }
      16r  { set w 2; set f S }
      32   { set w 4; set f i }
      32r  { set w 4; set f I }
      32f  { set w 4; set f r }
      32fr { set w 4; set f R }
      64   { set w 8; set f w }
      64r  { set w 8; set f W }
      64f  { set w 8; set f q }
      64fr { set w 8; set f Q }
    }
    #TODO reverse endianness on big-endian platforms not verified
    # may need to use a string map to reverse upper/lower for 16+ sizes
    binary scan $data @$off$f value
    return $value
  }

  # Decoder for Metakit datafiles in Tcl
  # requires Mmap/Mvec primitives

  namespace eval v {
    variable data
    variable seqn
    variable zero
    variable curr
    variable byte
    variable info ;# array
    variable node ;# array
    variable dbs  ;# array
  }

  proc Byte_seg {off len} {
    incr off $v::zero
    return [Mmap $v::data $off $len]
  }

  proc Int_seg {off cnt} {
    set vec [list 32r [Byte_seg $off [expr {4*$cnt}]]]
    return [Mvec $vec 0 $cnt]
  }

  proc Get_s {len} {
    set s [Byte_seg $v::curr $len]
    incr v::curr $len
    return $s
  }

  proc Get_v {} {
    set v 0
    while 1 {
      set char [Mvec $v::byte $v::curr]
      incr v::curr
      set v [expr {$v*128+($char&0xff)}]
      if {$char < 0} {
        return [incr v -128]
      }
    }
  }

  proc Get_p {rows vs vo} {
    upvar $vs size $vo off
    set off 0
    if {$rows == 0} {
      set size 0 
    } else {
      set size [Get_v]
      if {$size > 0} {
        set off [Get_v]
      }
    }
  }

  proc Header {{end ""}} {
    set v::zero 0
    if {$end eq ""} {
      set end [Mmap $v::data]
    }
    set v::byte [list 8 $v::data $v::zero $end]
    lassign [Int_seg [expr {$end-16}] 4] t1 t2 t3 t4
    set v::zero [expr {$end-$t2-16}]
    incr end -$v::zero
    set v::byte [list 8 $v::data $v::zero $end]
    lassign [Int_seg 0 2] h1 h2
    lassign [Int_seg [expr {$h2-8}] 2] e1 e2
    set v::info(mkend) $h2
    set v::info(mktoc) $e2
    set v::info(mklen) [expr {$e1 & 0xffffff}]
    set v::curr $e2
  }

  proc Layout {fmt} {
    regsub -all { } $fmt "" fmt
    regsub -all {(\w+)\[} $fmt "{\\1 {" fmt
    regsub -all {\]} $fmt "}}" fmt
    regsub -all {,} $fmt " " fmt
    return $fmt
  }

  proc DescParse {desc} {
    set names {}
    set types {}
    foreach x $desc {
      if {[llength $x] == 1} {
        lassign [split $x :] name type
        if {$type eq ""} {
          set type S } 
      } else {
        lassign $x name type
      }
      lappend names $name
      lappend types $type
    }
    return [list $names $types]
  }

  proc NumVec {rows type} {
    Get_p $rows size off
    if {$size == 0} {
      return {0 0}
    }
    set w [expr {int(($size<<3)/$rows)}]
    if {$rows <= 7 && 0 < $size && $size <= 6} {
      set widths {
        {8 16  1 32  2  4}
        {4  8  1 16  2  0}
        {2  4  8  1  0 16}
        {2  4  0  8  1  0}
        {1  2  4  0  8  0}
        {1  2  4  0  0  8}
        {1  2  0  4  0  0}
      }
      set w [lindex [lindex $widths [expr {$rows-1}]] [expr {$size-1}]]
    }
    if {$w == 0} {
      error "NumVec?"
    }
    switch $type F { set w 32f } D { set w 64f }
    incr off $v::zero
    return [list $w $v::data $off $rows]
  }

  proc Lazy_str {self rows type pos sizes msize moff index} {
    set soff {}
    for {set i 0} {$i < $rows} {incr i} {
      set n [Mvec $sizes $i]
      lappend soff $pos
      incr pos $n
    }
    if {$msize > 0} {
      set slen [Mvec $sizes 0 $rows]
      set v::curr $moff
      set limit [expr {$moff+$msize}]
      for {set row 0} {$v::curr < $limit} {incr row} {
        incr row [Get_v]
        Get_p 1 ms mo
        set soff [lreplace $soff $row $row $mo]
        set slen [lreplace $slen $row $row $ms]
      }
      set sizes [list lindex $slen $rows]
    }
    if {$type eq "S"} {
      set adj -1
    } else {
      set adj 0
    }
    set v::node($self) [list Get_str $soff $sizes $adj $rows]
    return [Mvec $v::node($self) $index]
  }

  proc Get_str {soff sizes adj index} {
    set n [Mvec $sizes $index]
    return [Byte_seg [lindex $soff $index] [incr n $adj]]
  }

  proc Lazy_sub {self desc size off rows index} {
    set v::curr $off
    lassign [DescParse $desc] names types
    set subs {}
    for {set i 0} {$i < $rows} {incr i} {
      if {[Get_v] != 0} {
        error "Lazy_sub?"
      }
      lappend subs [Prepare $types]
    }
    set v::node($self) [list Get_sub $names $subs $rows]
    return [Mvec $v::node($self) $index]
  }

  proc Get_sub {names subs index} {
    lassign [lindex $subs $index] rows handlers
    return [list Get_view $names $rows $handlers $rows]
  }

  proc Prepare {types} {
    set r [Get_v]
    set handlers {}
    foreach x $types {
      set n [incr v::seqn]
      lappend handlers $n
      switch $x {
        I - L - F - D {
          set v::node($n) [NumVec $r $x]
        }
        B - S {
          Get_p $r size off
          set sizes {0 0}
          if {$size > 0} {
            set sizes [NumVec $r I]
          }
          Get_p $r msize moff
          set v::node($n) [list Lazy_str $n $r $x $off $sizes $msize $moff $r]
        }
        default {
          Get_p $r size off
          set v::node($n) [list Lazy_sub $n $x $size $off $r $r]
        }
      }
    }
    return [list $r $handlers]
  }

  proc Get_view {names rows handlers index} {
    return [list Get_prop $names $rows $handlers $index [llength $names]]
  }

  proc Get_prop {names rows handlers index ident} {
    set col [lsearch -exact $names $ident]
    if {$col < 0} {
      error "unknown property: $ident"
    }
    set h [lindex $handlers $col]
    return [Mvec $v::node($h) $index]
  }

  proc dbopen {db file {fd ""}} {
    # open datafile, stores datafile descriptors and starts building tree
    if {$db eq ""} {
      set r {}
      foreach {k v} [array get v::dbs] {
        lappend r $k [lindex $v 0]
      }
      return $r
    }
    if {$fd eq ""} {
      set v::data [open $file]
    } else {
      set v::data $fd
    }
    set v::seqn 0
    Header
    if {[Get_v] != 0} {
      error "dbopen?"
    }
    set desc [Layout [Get_s [Get_v]]]
    lassign [DescParse $desc] names types
    set root [Get_sub $names [list [Prepare $types]] 0]
    set v::dbs($db) [list $file x$v::data $desc [Mvec $root 0]]
    if {$fd eq ""} {
      #close $v::data
    }
    return $db
  }

  proc dbclose {db} {
    # close datafile, get rid of stored info
    unset v::dbs($db)
    unset v::mmap_data($v::data)
  }

  proc dblayout {db} {
    # return structure description
    return [lindex $v::dbs($db) 2]
  }

  proc Tree {db} {
    # datafile selection, first step in access navigation loop
    return [lindex $v::dbs($db) 3]
  }

  proc access {spec} {
    # this is the main access navigation loop
    set s [split $spec ".!"]
    set x [list Tree [array size v::dbs]]
    foreach y $s {
      set x [Mvec $x $y]
    }
    return [namespace current]::$x
  }

  proc vnames {view} {
    # return a list of property names
    return [lindex $view 1]
  }

  proc vlen {view} {
    # return the number of rows in this view
    if {[namespace tail [lindex $view 0]] ne "Get_view"} {
      puts 1-$view
      puts 2-<[namespace tail [lindex $view 0]]>
      error "vlen?"
    }
    return [lindex $view 2]
  }
}

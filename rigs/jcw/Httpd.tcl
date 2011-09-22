Rig doc "Yet another httpd web server" {
  version "$Id$"
}

# Event triggers:
#   httpd.Accept $handler $sock - triggered on each new accepted connection
#   httpd.Busy $handler $sock - too many connections, about to drop this one
#   httpd.Lost $handler $sock - connection unexpectedly lost

# Note: the procs in this file are defined in the order as called in normal use

# the "v" namespace will also contain one child namespace per open connection
# all child namespaces are created in Accept and cleaned up in Done
namespace eval v {
  variable maxreqs  ;# maximum numer of simultaneous requests
  variable maxsecs  ;# maximum time before a request times out
}

# start listening to new requests
proc start {port handler {iface 0.0.0.0}} {
  stop
  #TODO make configurable
  set v::maxreqs 5
  set v::maxsecs 600
  set cmd [namespace code [list Accept $handler]]
  lappend v::listen [socket -server $cmd -myaddr $iface $port]
}

# accept a new connection and setup a request namespace for it
proc Accept {handler sock addr port} {
  if {[llength [namespace children v]] >= $v::maxreqs} {
    Rig notify httpd.Busy $handler $sock ;# hook can generate a response
    close $sock
  } else {
    namespace eval v::$sock {
      namespace export -clear * ;# requests also allow uppercase sub-commands
      namespace ensemble create
    }
    variable v::${sock}::request
    set request(handler) $handler
    set timeout ${v::maxsecs}000
    set request(cancel) [after $timeout [namespace code "Respond $sock {} 408"]]
    fconfigure $sock -blocking 0 -translation {auto crlf}
    fileevent $sock readable [namespace code "RecvFirst $sock"]
    Rig notify httpd.Accept $handler $sock
  }
}

# called when the first line of the request is coming in
proc RecvFirst {sock} {
  variable v::${sock}::request
  if {[gets $sock line] < 0 && [eof $sock]} {
    Done $sock
  } elseif {![fblocked $sock]} {
    if {[regexp {^([A-Z]+) ([^?]+)\??([^ ]*) HTTP/1.([01])$} $line - \
            request(proto) request(url) request(query) request(version)]} {
      fileevent $sock readable [namespace code "RecvLine $sock"]
    } else {
      return -code 400 $line
    }
  }
}

# called when subsequent mime headers are coming in
proc RecvLine {sock} {
  variable v::${sock}::request
  variable v::${sock}::info
  variable v::${sock}::query ""
  if {[gets $sock line] < 0 && [eof $sock]} {
    Rig notify httpd.Lost $request(handler) $sock ;# connection lost
    Done $sock
  } elseif {![fblocked $sock]} {
    if {$line ne ""} {
      if {[regexp {([^:]+):\s*(.*)} $line - key val]} {
        set key [string tolower $key]
        set request(key) $key
        if {[info exists info($key)]} {
          append info($key) ", "
        }
        append info($key) $val
      } elseif {[regexp {^\s+(.+)} $line - val] && [info exists request(key)]} {
        append info($request(key)) " " $val
      } else {
        return -code 400 $line
      }
    } else {
      if {$request(proto) ne "POST"}  {
        Ready $sock
      } elseif {[info exists info(content-length)]} {
        fconfigure $sock -translation {binary crlf}
        fileevent $sock readable [namespace code "RecvData $sock"]
      } else {
        return -code 411
      }
    }
  }
}

# called whenever there is POST data to process
proc RecvData {sock} {
  variable v::${sock}::info
  variable v::${sock}::query ;#TODO rename to post, clashes with request(query)
  set remaining [expr {$info(content-length) - [string length $query]}]
  append query [read $sock $remaining]
  if {[eof $sock]} {
    Done $sock
  } elseif {[string length $query] == $info(content-length)} {
    Ready $sock
  }
}

# called once a request is ready for processing
proc Ready {sock} {
  set v::${sock}::headers {Content-Type "text/html; charset=utf-8"}
  set obj [namespace current]::v::$sock
  # define a convenient delegation method
  interp alias {} ${obj}::delegate {} {*}[namespace code Delegate] $obj
  # then add the respond method with it
  $obj delegate respond Respond $sock
  # perform the callback, handling error return codes 100..up as status codes
  set err [catch { {*}[set v::${sock}::request(handler)] $obj } msg]
  if {$err} {
    Rig catcher Respond $sock $msg [expr {$err < 100 ? 500 : $err}]
  }
}

# utility code to easily delegate additional request methods
proc Delegate {obj method args} {
  interp alias {} ${obj}::$method {} {*}[uplevel [list namespace code $args]]
}

# called to return page contents, redirections, errors, and so on
proc Respond {sock result {status "200 OK"} {async ""}} {
  # async file sending works by calling Respond explicitly - this means that
  # a 2nd implicit call done when the request handler returns must be ignored
  if {[incr v::${sock}::replied] > 1} {
    return
  }
  if {[string length $status] == 3} {
    set status [Web fullresponse $status]
  }
  if {$status >= "400 "} {
    Rig ifdef v::${sock}::failed {
      set result [Rig catcher v::$sock failed $status $result]
    }
  }
  puts $sock "HTTP/1.0 $status"
  foreach {key val} [set v::${sock}::headers] {
    if {$key eq "Content-Type"} {
      set textmode [string match text/* $val]
    }
    puts $sock "$key: $val"
  }
  puts $sock ""
  if {$textmode} {
    fconfigure $sock -encoding utf-8
  } else {
    fconfigure $sock -translation binary
  }
  puts -nonewline $sock $result
  if {$async ne ""} {
    return [list $sock [namespace code "Done $sock"]]
  }  
  Done $sock
}

# called whenever a request has been completed, with or without errors
proc Done {sock} {
  after cancel [set v::${sock}::request(cancel)]
  Rig ifdef v::${sock}::done {
    Rig catcher v::$sock done
  }
  read $sock
  close $sock
  namespace delete v::$sock
}

# stop accepting new requests, pending requests still complete normally
proc stop {} {
  if {[info exists v::listen]} {
    foreach fd $v::listen {
      close $fd
    }
    unset v::listen
  }
}

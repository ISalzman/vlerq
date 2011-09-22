Rig doc "General-purpose web parsing, rendering, and response utilities" {
  version "$Id$"
}

# Event triggers:
#   web.Sent $fd $cmd $bytes - triggered when async sendfile is done

# see http://en.wikipedia.org/wiki/List_of_HTTP_status_codes 
# and http://www.google.com/support/webmasters/bin/answer.py?hl=en&answer=40132
array set statusmsgs {
  100 "Continue"
  101 "Switching Protocols"

  200 "OK"
  201 "Created"
  202 "Accepted"
  203 "Non-Authoritative Information"
  204 "No Content"
  205 "Reset Content"
  206 "Partial Content"

  300 "Multiple Choices"
  301 "Moved Permanently"
  302 "Found"
  303 "See Other"
  304 "Not Modified"
  305 "Use Proxy"
  306 "Switch Proxy"
  307 "Temporary Redirect"

  400 "Bad Request"
  401 "Unauthorized"
  402 "Payment Required"
  403 "Forbidden"
  404 "Not Found"
  405 "Method Not Allowed"
  406 "Not Acceptable"
  407 "Proxy Authentication Required"
  408 "Request Timeout"
  409 "Conflict"
  410 "Gone"
  411 "Length Required"
  412 "Precondition Failed"
  413 "Request Entity Too Large"
  414 "Request-URI Too Long"
  415 "Unsupported Media Type"
  416 "Requested Range Not Satisfiable"
  417 "Expectation Failed"
  
  500 "Internal Server Error"
  501 "Not Implemented"
  502 "Bad Gateway"
  503 "Service Unavailable"
  504 "Gateway Timeout"
  505 "HTTP Version not supported"
}

# extend raw http response code to include a comment, if not already present
proc fullresponse {status} {
  if {[string length $status] == 3} {
    variable statusmsgs
    append status " " [Rig get statusmsgs($status) "Other"]
  }
  return $status
}

# escape text for html use
proc htmlize {text} {
  return [string map {& &amp; < &lt; > &gt; \" &quot; ' &#39;} $text]
}

# decode www-url-encoded strings
proc urldecode {str} {
  set str [string map {+ { } \\ \\\\} $str]
  set str [regsub -all -nocase {%([A-F0-9][A-F0-9])} $str {\\u00\1}]
  return [string map {\r\n \n} [subst -novariables -nocommands $str]]
}

# return a redirect response
proc redirect {obj url} {
  dict set ${obj}::headers Location $url
  $obj respond "Redirect to <a href='$url'>$url</a>" 302
  #TODO consider: REQ H Location $url (no need to pass $obj in as arg)
  #TODO consider: return -code 302 "Redirect to <a href='$url'>$url</a>"
}

# send a file in the background, as response, and clean up when done
proc sendfile {obj file} {
  set size [file size $file]
  dict set ${obj}::headers Content-Length $size
  set fd [open $file]
  fconfigure $fd -translation binary
  lassign [$obj respond "" "200 Data Follows" -async] sock cmd
  fconfigure $sock -translation binary ;# otherwise file size is inaccurate
  fcopy $fd $sock -size $size -command [namespace code [list SendEnd $fd $cmd]]
}

# called when file send completes, with $cmd usually referring to Httpd::Done
proc SendEnd {fd cmd bytes {err {}}} {
  Rig notify web.Sent $fd $cmd $bytes
  close $fd
  eval $cmd
}

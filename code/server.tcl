Rig hook main.Init {Rig options {port 4724}}

Rig hook main.Run {
  Render setup [Rig home]/site
  Httpd start [Rig config port] [namespace code OnWebRequest]
  Rig hook httpd.Accept "Rig reload ;#"
}

proc OnWebRequest {obj} {
  variable ${obj}::request
  if {$request(url) eq "/favicon.ico"} {
    return -code 404
  }
  Log: {[namespace tail $obj] $request(proto) $request(url) $request(query)} 
  $obj delegate failed [namespace which OnFailedRequest] $obj
  Render dispatch $obj
  $obj respond [$obj NEXT]
}

proc OnFailedRequest {obj code errmsg} {
  variable ${obj}::request
  set e $::errorInfo
  Log: {FailedRequest: [string range [regsub -all {\s+} $e " "] 0 199]...}
  return "<h1>Error $code</h1><h3>$errmsg</h3>
          <p>While trying to obtain <b>$request(url)</b></p>
          <hr size=1><pre>[Web htmlize $e]</pre>"
}

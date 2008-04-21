set opt(run) 1


set opt(channel)	1
set opt(radio)		1

set opt(chan-data-per)	0
set opt(chan-ctrl-per)	0
#set opt(propagation)	4
set opt(propagation)	4

set opt(sym-duration)	20
set opt(sym-perframe)	500

set opt(control)	4

set opt(cfg-interval)	16

set opt(msh-dsch-avg-bad) -1
set opt(msh-dsch-avg-good) -1





# bwmanager

set opt(bwmanager)	"fair-rr"
set opt(availabilities)	"off"
set opt(regrant)		"off"
set opt(regrant-offset)	1
set opt(regrant-duration) 1
set opt(grant-fairness)	"off"
set opt(regrant-same-horizon) "off"
set opt(regrant-fairness)	"off"
set opt(request-fairness)	"off"
set opt(bwm-round-duration)	21312
set opt(weight-timeout)	120
set opt(max-deficit)	0
set opt(max-backlog)	0
set opt(weight-flow)	"off"
set opt(grant-rnd-channel) "off"
set opt(dd-timeout)	"50"
set opt(min-grant)	"1"


set opt(coordinator)	"standard"
set opt(holdoff-dsch-def) 0
set opt(holdoff-ncfg-def) 0
set opt(max-advertised) -1


set opt(forwarding)	"spf"
set opt(maxnumhops)	20
set opt(profileupdateperiod)	4
set opt(samplingperiod)	4
set opt(chi)	0.5
set opt(beta)	0.5


set opt(scheduler)	"fifo"
set opt(buffer)	1000000



set opt(prfall)	0
set opt(prfsrc) {}
set opt(prfdst) {}
set opt(prfndx) {}





set opt(warm) 2.0
set opt(duration) 10.0

set opt(random-nodeid) "off"




proc getopt {argc argv} {
    global opt

    for {set i 0} {$i < $argc} {incr i} {
	set arg [lindex $argv $i]
	if {[string range $arg 0 0] != "-"} continue

	set name [string range $arg 1 end]
	set opt($name) [lindex $argv [expr $i+1]]
    }
}


proc init {} {
    global opt ns simtime


    set ns [new Simulator]
    #$defaultRNG seed 1

    $ns run-identifier $opt(run)
    $ns stat file "out"
    $ns at $opt(warm) "$ns stat on"
    $ns at $opt(duration) "finish"
    
    set opt(trace) [open "trace.out" w]
    set simtime [clock seconds]
    $ns use-newtrace
    $ns trace-all $opt(trace)
}

proc finish {} {
    global ns simtime


    set simtime [expr [clock seconds] - $simtime]
    puts "run duration: $simtime s"
    exit 0

}
proc create_topology {} {
    global ns opt topo map invmap

    
    set topo_unused [new Topography]
    
    set chan_unused [new Channel/WirelessChannel]
    puts "node-config"
    $ns node-config -llType LL -macType WimshMac -adhocRouting DumbAgent

    $ns node-config \
	-ifqType Queue/DropTail/PriQueue \
	-ifqLen 50 \
	-antType Antenna/OmniAntenna \
	-propType Propagation/TwoRayGround \
	-phyType Phy/WirelessPhy \
	-topoInstance $topo_unused \
	-agentTrace OFF \
	-routerTrace OFF \
	-macTrace OFF \
	-movementTrace OFF \
	-channel $chan_unused

    set topo [new WimshTopology/Simple]
    puts "create topo"
    #set topology nodes
    set opt(nodes) 5

    


    if { $opt(random-nodeid) == "on" } {
	for {set i 0} {$i < $opt(nodes) } {incr i} {
	    while { 1 } {
		set x [$defaultRNG integer $opt(nodes)]
		set found 0
		for { set j 0 } { $j < $i } {incr j} {
		    if {$map($j) == $x } {
			set found 1
			break
		    }
		}
		if { $found == 0 } {
		    set map($i) $x
		    break
		}
	    }
	}
	
	for{ set i 0 } { $i < $opt(nodes) } { incr i } {
	    set invmap($map($i)) $i
	}
    } else {
	puts "random off"
	for { set i 0 } {$i < $opt(nodes) } { incr i } {
	    set map($i) $i
	    set invmap($i) $i
	}
    }
    $topo connect $map(0) $map(1)
    $topo connect $map(0) $map(2)
    $topo connect $map(1) $map(3)
    $topo connect $map(1) $map(4)
    
    puts "before init"
    $topo initialize
    puts "after"
}

#########create nodes
proc create_nodes {} {
    global opt ns topo map invmap macmib node
    
    set T $opt(sym-duration)
    set f [expr 1e-3 * $opt(sym-duration) * $opt(sym-perframe)]
    
    
    set phymib [new WimshPhyMib]
    $phymib symDuration $T
    $phymib frameDuration $f
    $phymib controlSlots $opt(control)
    $phymib cfg-interval $opt(cfg-interval)
    $phymib recompute
    
    set macmib [new WimshMacMib]
    
    $macmib phymib $phymib
    
    for {set j 0} {$j < $opt(channel)} {incr j} {
	set chan($j) [new WimshChannel]
	$chan($j) topology $topo
	$chan($j) propagation $opt(propagation)
	$chan($j) id [expr 1 + $j]
	if {$opt(chan-data-per) != 0} {
	    $chan($j) error data $opt(chan-data-per)
	}
	if {$opt(chan-ctrl-per) != 0} {
	    $chan($j) error control $opt(chan-ctrl-per)
	}
    }


    for {set i 0} {$i < $opt(nodes)} {incr i} {
	if { $i == 0 } {
	    $ns node-config -macType WimshMac/BSWimshMac
	    puts "before bsmac $invmap($i)"
	    set node($invmap($i)) [$ns node]
	    puts "after bsmac"
	    $ns node-config -macType WimshMac/SSWimshMac
	} else {
	    set node($invmap($i)) [$ns node]
	}
	
	set mac($i) [$node($invmap($i)) getMac 0]
	
	$mac($i) index $invmap($i)
    
	$mac($i) macmib $macmib
	$mac($i) phymib $phymib
    
	$mac($i) topology $topo
    
	for { set j 0 } {$j < $opt(channel) } { incr j } {
	    $mac($i) channel $chan($j)
	}
    
	for { set j 0 } {$j < $opt(radio) } { incr j } {
	    set phy [new WimshPhy]
	    $phy mac $mac($i)
	    $phy topology $topo
	    $phy phymib $phymib
	    $phy epsilon 5
	    $mac($i) phy $phy
	}
    
	$mac($i) forwarding $opt(forwarding)
	$mac($i) bwmanager $opt(bwmanager)
	$mac($i) scheduler $opt(scheduler)
	$mac($i) coordinator $opt(coordinator)
    
	#turn dsch off
	$mac($i) coordinator nextxmttime -1
	$mac($i) coordinator max-advertised $opt(max-advertised)
    
	$mac($i) coordinator mode "xmtunaware"
    
	#configure bwmanager
	$mac($i) bwmanager availabilities $opt(availabilities)
	$mac($i) bwmanager fairness no
	$mac($i) bwmanager regrant $opt(regrant)
	$mac($i) bwmanager regrant-offset $opt(regrant-offset)
	$mac($i) bwmanager regrant-duration $opt(regrant-duration)
	$mac($i) bwmanager regrant-same-horizon "off"
	$mac($i) bwmanager round-duration $opt(bwm-round-duration)
	$mac($i) bwmanager max-deficit $opt(max-deficit)
	$mac($i) bwmanager max-backlog $opt(max-backlog)
    
	$mac($i) bwmanager dd-timeout $opt(dd-timeout)
	$mac($i) bwmanager min-grant $opt(min-grant)
	
	$mac($i) bwmanager wm weight-flow $opt(weight-flow)
	
    
	#configure scheduler
	$mac($i) scheduler size $opt(buffer)
    
	$mac($i) msh-dsch-avg-bad $opt(msh-dsch-avg-bad)
	$mac($i) msh-dsch-avg-good $opt(msh-dsch-avg-good)
    
	$mac($i) initialize
    }
}




proc create_connections {} {
    global ns opt macmib node topo

    set app [new Application/Traffic/CBR]
    $app set packetSize_ 1000
    $app set rate_ 100000

    set agtsrc [new Agent/UDP]
    set agtdst [new Agent/UDP]

    $agtsrc set class_ 0

    $ns at 2 "$app start"
    $ns at 9.1 "$app stop"
    
    $macmib crc 0 crc
    $macmib priority 0 1
    $macmib precedence 0 0
    
    $ns attach-agent $node(3) $agtsrc
    $ns attach-agent $node(2) $agtdst
    $ns connect $agtsrc $agtdst
    $app attach-agent $agtsrc

    


    set app1 [new Application/Traffic/CBR]
    $app1 set packetSize_ 1000
    $app1 set rate_ 100000
    
    set agtsrc1 [new Agent/UDP]
    set agtdst1 [new Agent/UDP]
    $agtsrc1 set class_ 1
    
    $ns at 3.1 "$app1 start"
    $ns at 8.0 "$app1 stop"

    $macmib crc 1 crc
    $macmib priority 1 1
    $macmib precedence 1 0

    $ns attach-agent $node(2) $agtsrc1
    $ns attach-agent $node(4) $agtdst1
    $ns connect $agtsrc1 $agtdst1
    $app1 attach-agent $agtsrc1
}



proc create_profiles {} {
    global ns opt node map phymib


    for { set i 0 } { $i < $opt(nodes) } { incr i } {
	[$node($i) getMac 0] profile $opt(prfall) all
    }


    for { set i 0 } { $i < [llength $opt(prfsrc)] } { incr i } {
	set macsrc [$node([lindex $opt(prfsrc) $i]) getMac 0]
	set macdst [$node([lindex $opt(prfdst) $i]) getMac 0]
	$macsrc profile [lindex $opt(prfndx) $i] $map([lindex $opt(prfdst) $i])
	$macdst profile [lindex $opt(prfndx) $i] $map([lindex $opt(prfsrc) $i])
    }

}


proc alive {} {
    global ns opt

    if { [$ns now] != 0} {
	puts -nonewline \
	    [format "elapsed %.0f s (remaining %.0f s) completed %.f%%" \
		 [$ns now] \
		 [expr $opt(duration) - [$ns now]] \
		 [expr 100 * [$ns now] / $opt(duration)]]
	if { [$ns now] >= $opt(warm)}  {
	    puts " stat collection ON"
	} else {
	    puts ""
	}
    }
    $ns at [expr [$ns now] + $opt(duration) / 10.0] "alive"
}



#getopt $argc $argv

init
puts "initial"
create_topology
puts "create topology"
create_nodes
puts "create node"


create_connections
create_profiles

alive

puts "running"

$ns run
package.path = package.path ..";?.lua;test/?.lua;app/?.lua;"

--pktgen.page("range");

-- Port 0 3c:fd:fe:9c:5c:d8,  Port 1 3c:fd:fe:9c:5c:b8
pktgen.range.dst_mac("0", "start", "ec:0d:9a:a4:1c:e6");
-- pktgen.range.dst_mac("0", "start", "ec:0d:9a:a4:1c:e6");
pktgen.range.src_mac("0", "start", "3c:fd:fe:9c:5c:aa");

pktgen.range.vlan_id("0", "start", 10);
pktgen.range.vlan_id("0", "inc", 0);
pktgen.range.vlan_id("0", "min", 10);
pktgen.range.vlan_id("0", "max", 10);

pktgen.range.dst_ip("0", "start", "51.15.1.2");
pktgen.range.dst_ip("0", "inc", "0.0.0.1");
pktgen.range.dst_ip("0", "min", "51.15.1.2");
pktgen.range.dst_ip("0", "max", "51.15.200.200");

pktgen.range.src_ip("0", "start", "200.168.0.1");
pktgen.range.src_ip("0", "inc", "0.0.0.1");
pktgen.range.src_ip("0", "min", "200.168.0.1");
pktgen.range.src_ip("0", "max", "200.168.200.200");

pktgen.set_proto("0", "udp");

pktgen.range.dst_port("0", "start", 2000);
pktgen.range.dst_port("0", "inc", 1);
pktgen.range.dst_port("0", "min", 2000);
pktgen.range.dst_port("0", "max", 4000);

pktgen.range.src_port("0", "start", 5000);
pktgen.range.src_port("0", "inc", 1);
pktgen.range.src_port("0", "min", 5000);
pktgen.range.src_port("0", "max", 7000);

pktgen.range.pkt_size("0", "start", 64);
-- pktgen.range.pkt_size("0", "start", 512);
-- pktgen.range.pkt_size("0", "inc", 0);
-- pktgen.range.pkt_size("0", "min", 64);
-- pktgen.range.pkt_size("0", "max", 256);

-- Set up second port
pktgen.range.dst_mac("1", "start", "3c:fd:fe:a5:80:99");
pktgen.range.src_mac("1", "start", "3c:fd:fe:9c:5c:ff");

pktgen.range.src_ip("1", "start", "10.8.1.2");
pktgen.range.src_ip("1", "inc", "0.0.0.1");
pktgen.range.src_ip("1", "min", "10.8.1.2");
pktgen.range.src_ip("1", "max", "10.8.200.200");

pktgen.range.dst_ip("1", "start", "200.168.0.1");
pktgen.range.dst_ip("1", "inc", "0.0.0.1");
pktgen.range.dst_ip("1", "min", "200.168.0.1");
pktgen.range.dst_ip("1", "max", "200.168.200.200");

pktgen.set_proto("1", "udp");

pktgen.range.dst_port("1", "start", 6000);
pktgen.range.dst_port("1", "inc", 1);
pktgen.range.dst_port("1", "min", 6000);
pktgen.range.dst_port("1", "max", 9000);

pktgen.range.src_port("1", "start", 1000);
pktgen.range.src_port("1", "inc", 1);
pktgen.range.src_port("1", "min", 1000);
pktgen.range.src_port("1", "max", 3000);

-- pktgen.range.vlan_id("1", "start", 10);

pktgen.range.pkt_size("1", "start", 64);
-- pktgen.range.pkt_size("0", "inc", 0);
-- pktgen.range.pkt_size("0", "min", 64);
-- pktgen.range.pkt_size("0", "max", 256);


pktgen.set_range("all", "on");

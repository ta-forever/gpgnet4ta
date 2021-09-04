-- trivial protocol example
-- declare our protocol
tasmartpak_proto = Proto("tasmartpak","TA Smartpak")

FROMID = ProtoField.uint32("tasmartpak.fromid", "fromid", base.HEX)
TOID = ProtoField.uint32("tasmartpak.toid", "toid", base.HEX)
COMPRESSION = ProtoField.uint8("tasmartpak.compression", "compression", base.HEX)
CHECKSUM = ProtoField.uint16("tasmartpak.checksum", "checksum", base.HEX)
DATAENCRYPTED = ProtoField.bytes("tasmartpak.dataenc", "dataenc", base.NONE)
TICKS = ProtoField.uint32("tasmartpak.ticks", "ticks", base.HEX)
SUBPAKS = ProtoField.bytes("tasmartpak.subpaks", "subpaks", base.NONE)


tasmartpak_proto.fields = {FROMID, TOID, COMPRESSION, CHECKSUM, DATAENCRYPTED, TICKS, SUBPAKS}

-- create a function to dissect it
function tasmartpak_proto.dissector(buffer,pinfo,tree)
    local base_ofs = 20
    length = buffer:len() - base_ofs
    pinfo.cols.protocol = tasmartpak_proto.name
    local subtree = tree:add(tasmartpak_proto,buffer(),"TA Smartpak")
    
    local from_id = buffer(base_ofs,4):uint()
    local to_id = buffer(base_ofs+4,4):uint()
    local compression = buffer(base_ofs+8,1)
    local checksum = buffer(base_ofs+9,2)
    local encrypted = buffer(base_ofs+11,length-11)
    local decrypted = ByteArray.new()
    decrypted:set_size(encrypted:len())

    local key = 3
    for i = 0,encrypted:len()-1
    do 
        local x = encrypted(i,1):uint()
        if i <= encrypted:len()-4
        then
            x = bit32.bxor(x,key)
        end
        decrypted:set_index(i,x)
        key = key + 1
    end

    subtree:add(FROMID, from_id)
    subtree:add(TOID, to_id)
    subtree:add(COMPRESSION, compression)
    subtree:add(CHECKSUM, checksum)
    subtree:add(DATAENCRYPTED, encrypted)
    
    local tvb = ByteArray.tvb(decrypted, "decrypted")
    subtree:add(TICKS, tvb(0,4):uint())
    subtree:add(SUBPAKS, tvb(4,decrypted:len()-4))
end

udp_table = DissectorTable.get("tcp.port")
udp_table:add("2301",tasmartpak_proto)

-- trivial protocol example
-- declare our protocol
tafnet_proto = Proto("tafnet","TAFNet Protocol")

ACTION = ProtoField.uint8("tafnet.action", "action", base.DEC)
SEQ = ProtoField.uint32("tafnet.seq", "seq", base.DEC)
DATA = ProtoField.bytes("tafnet.data", "data", base.NONE)

tafnet_proto.fields = {ACTION, SEQ, DATA}

local get_action_name = 
{
  [0] = "INVALID",
  [2] = "UDP_DATA",
  [3] = "TCP_OPEN",
  [4] = "TCP_CLOSE",
  [5] = "TCP_DATA",
  [6] = "TCP_ACK",
  [7] = "TCP_RESEND",
  [8] = "TCP_SEQ_REBASE",
  [9] = "ENUM"
  [10] = "UDP_PROTECTED",
  [11] = "PACKSIZE_TEST",
  [12] = "PACKSIZE_ACK",
  [13] = "HELLO"
}

-- create a function to dissect it
function tafnet_proto.dissector(buffer,pinfo,tree)
    length = buffer:len()
    pinfo.cols.protocol = tafnet_proto.name
    local subtree = tree:add(tafnet_proto,buffer(),"Tafnet Protocol")
    
    local action_number = buffer(0,1):uint()
    local action_name = get_action_name[action_number]
    subtree:add(ACTION, action_number):append_text(" (" .. action_name .. ")")

    if action_number >= 6 then
        subtree:add_le( SEQ, buffer(4,4) )
        subtree:add( DATA, buffer(8, length-8) )
    else
        subtree:add( DATA, buffer(1, length-1) )
    end
end

udp_table = DissectorTable.get("udp.port")
udp_table:add(6112,tafnet_proto)

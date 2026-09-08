import socket, struct, fcntl, sys
SIOCGMIIPHY=0x8947; SIOCGMIIREG=0x8948; SIOCSMIIREG=0x8949
ifn=sys.argv[1].encode()
s=socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
def ifr(phy,reg,val):
    return struct.pack('16sHHHH24x', ifn, phy, reg, val, 0)
r=fcntl.ioctl(s, SIOCGMIIPHY, ifr(0,0,0)); phy=struct.unpack('16sHHHH24x', r)[1]
def rd(reg): return struct.unpack('16sHHHH24x', fcntl.ioctl(s, SIOCGMIIREG, ifr(phy,reg,0)))[4]
def wr(reg,val): fcntl.ioctl(s, SIOCSMIIREG, ifr(phy,reg,val))
def rdext(e): wr(0x1e,e); return rd(0x1f)
print('phy addr', phy, 'id %04x%04x' % (rd(2),rd(3)))
print('std:', ' '.join('r%02x=%04x' % (i, rd(i)) for i in (0,1,4,5,6,9,10,0x11)))
for e in (0x0c,0x27,0x57,0xa000,0xa001,0xa003,0xa010,0xa012,0xa039,0xa03a,0xa03b,0xa03c,0xa03d,0xa03e,0xa03f,0xa040,0xa041):
    print('ext %04x = %04x' % (e, rdext(e)))

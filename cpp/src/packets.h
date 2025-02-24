#include <boost/shared_ptr.hpp>
#include "cmds.h"
#include "vchpack.h"
#include "stdint.h"
#include "events.h"

#ifndef PACKETS_H
#define PACKETS_H

using namespace std;

class Packet
{
public:
    virtual unsigned char typechar();

    virtual void pack(vch *dest);
    virtual void unpackAndMoveIter(vchIter *iter);

    void packPacket(vch *destVch);
    void unpackPacketAndMoveIter(vchIter *iter);

    Packet();
    Packet(vchIter *iter);
};

// note that a resync packet is so simple that it's just made directly in server and client

struct FrameEventsPacket : public Packet
{
    unsigned char typechar();

    uint64_t frame;
    vector<boost::shared_ptr<AuthdCmd>> authdCmds;
    vector<boost::shared_ptr<Event>> events;

    void pack(vch *dest);
    void unpackAndMoveIter(vchIter *iter);

    FrameEventsPacket(uint64_t frame, vector<boost::shared_ptr<AuthdCmd>> authdCmds, vector<boost::shared_ptr<Event>> events);
    FrameEventsPacket(vchIter *iter);
};

#endif // PACKETS_H
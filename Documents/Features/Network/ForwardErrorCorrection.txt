Forward Error Correction (FEC) for Server Coord Updates
=========================================================
NOTE: Line numbers indicative only — anchor on symbols.
Priority: LOW — Only implement if WAN packet loss consistently exceeds 2%.
         The existing resend system (ACK bitfield + SendResends, ServerSend.cpp:159) handles LAN well.

Concept
-------
XOR parity: for every N consecutive data packets on a given slot, compute a parity
packet as the bitwise XOR of all N payloads. If any single packet in the group is
lost, the client reconstructs it from the remaining N-1 data packets plus the parity
packet. Cost: 1/N bandwidth overhead per slot.

FEC groups are per-coord-per-client because Server::SendUpdate() (ServerSend.cpp:124)
sends one unreliable packet per active subscription slot per tick. Each slot already
has independent ACK tracking (AckState in coordAckStates), so FEC state naturally
lives alongside it.

Adaptive group size: start with N=8 (12.5% overhead). If observed loss rate on a
slot exceeds 5%, shrink to N=4 (25% overhead). If loss rate drops below 1%, grow
to N=16 (6.25% overhead). Clamp to [4, 16].

FILES MODIFIED
==============

1. Engine/Source/Network/NetworkProtocol.h
-----------------------------------------
Add new packet type to the PacketType enum (line 7-25):

    kServerFecParity,           // Per-coord FEC parity packet (unreliable, slot channel)

Add after line 24 (before the closing brace of the enum).

Add FEC constants after the existing protocol constants (after line 42):

    inline constexpr int64_t kiFecGroupSizeMin = 4;
    inline constexpr int64_t kiFecGroupSizeMax = 16;
    inline constexpr int64_t kiFecGroupSizeDefault = 8;
    inline constexpr float kfFecShrinkLossThreshold = 0.05f;  // Shrink group if loss > 5%
    inline constexpr float kfFecGrowLossThreshold = 0.01f;    // Grow group if loss < 1%


2. Engine/Source/Network/Server/Server.h
----------------------------------------
Add FEC tracking state to ClientConnection (line 16-68):

    // FEC parity state per coord slot
    struct FecSlotState
    {
        int64_t iGroupStartTick = 0;     // First tick in current FEC group
        int64_t iGroupCount = 0;          // Packets sent in current group so far
        int64_t iGroupSize = kiFecGroupSizeDefault;  // Adaptive N
        int64_t iMaxPayloadSize = 0;      // Largest payload in current group (for XOR padding)
        std::vector<uint8_t> parityBuffer; // Running XOR accumulator
    };

Add to ClientConnection struct (after coordAckStates, line 25):

    std::vector<FecSlotState> coordFecStates;

Initialize in Server::Connect() (resize calls at Server.cpp:129-132), add after coordAckStates resize:

    connection.coordFecStates.resize(NetworkManager::kiMaxEnetCoordSlots);

Reset FEC state in ClientConnection::FreeSlot() (Server.h line 56-62), add:

    coordFecStates.at(iSlot) = {};

Add private method declaration to Server class (after line 131):

    void SendFecParity(ClientConnection& rClient, int64_t iSlot, uint16_t uiEpoch);
    void AccumulateFecParity(ClientConnection& rClient, int64_t iSlot, std::span<const uint8_t> packetData);


3. Engine/Source/Network/Server/ServerSend.cpp
----------------------------------------------
Modify Server::SendUpdate() (line 124):

After the enet_peer_send call (line 150; the resend path's send is at line 233), before
rWorkbuffer.Pop(), accumulate the
packet payload into the FEC parity buffer and check if the group is complete:

    // FEC: accumulate parity and send parity packet when group is complete
    AccumulateFecParity(rClient, iSlot, packetSpan);

Add new methods at end of file (before the closing namespace brace, line 301):

    void Server::AccumulateFecParity(ClientConnection& rClient, int64_t iSlot, std::span<const uint8_t> packetData)
    {
        FecSlotState& rFec = rClient.coordFecStates.at(iSlot);

        if (rFec.iGroupCount == 0)
        {
            rFec.iGroupStartTick = /* current tick from the packet header */;
            rFec.parityBuffer.assign(packetData.begin(), packetData.end());
            rFec.iMaxPayloadSize = std::ssize(packetData);
        }
        else
        {
            // XOR into parity buffer, extending if this packet is larger
            if (std::ssize(packetData) > std::ssize(rFec.parityBuffer))
            {
                rFec.parityBuffer.resize(packetData.size(), 0);
            }
            rFec.iMaxPayloadSize = std::max(rFec.iMaxPayloadSize, std::ssize(packetData));
            for (int64_t i = 0; i < std::ssize(packetData); ++i)
            {
                rFec.parityBuffer.at(i) ^= packetData[i];
            }
        }

        ++rFec.iGroupCount;

        if (rFec.iGroupCount >= rFec.iGroupSize)
        {
            SendFecParity(rClient, iSlot, rClient.coordAckStates.at(iSlot).uiEpoch);
            rFec.iGroupCount = 0;
            rFec.parityBuffer.clear();
            rFec.iMaxPayloadSize = 0;
        }
    }

    void Server::SendFecParity(ClientConnection& rClient, int64_t iSlot, uint16_t uiEpoch)
    {
        FecSlotState& rFec = rClient.coordFecStates.at(iSlot);

        common::Workbuffer& rWorkbuffer = common::gpThreadLocal->mWorkbuffer;
        rWorkbuffer.Push();

        // [1B type][1B slotIndex][2B epoch][8B groupStartTick][1B groupSize][4B paritySize][...parity data]
        rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(PacketType::kServerFecParity));
        rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(iSlot));
        rWorkbuffer.PushBack<uint16_t>(uiEpoch);
        rWorkbuffer.PushBack<int64_t>(rFec.iGroupStartTick);
        rWorkbuffer.PushBack<uint8_t>(static_cast<uint8_t>(rFec.iGroupSize));
        rWorkbuffer.PushBack<int32_t>(static_cast<int32_t>(rFec.iMaxPayloadSize));
        rWorkbuffer.Append(std::string_view(reinterpret_cast<const char*>(rFec.parityBuffer.data()), rFec.iMaxPayloadSize));

        std::span<const uint8_t> packetSpan = rWorkbuffer.Span<uint8_t>();

        // Heap: ENet allocates packet data internally
        ScopedSuppressAllocationTracking suppress;
        ENetPacket* pPacket = enet_packet_create(packetSpan.data(), packetSpan.size(), 0);
        enet_peer_send(rClient.pPeer, NetworkManager::CoordSlotUnreliable(iSlot), pPacket);

        rWorkbuffer.Pop();
    }


4. Engine/Source/Network/Client/Client.h
----------------------------------------
Add FEC receive state to ClientCoordSlot (line 45-50):

    // FEC receive state
    struct FecReceiveState
    {
        int64_t iGroupStartTick = 0;
        int64_t iGroupSize = 0;
        int64_t iMaxPayloadSize = 0;
        int64_t iReceivedCount = 0;
        int64_t iMissingIndex = -1;       // Which index in group is missing (-1 = none yet)
        std::vector<uint8_t> parityAccumulator; // XOR of all received data packets in this group
    };
    FecReceiveState fecReceive;

Add private method declaration to Client class (after line 109):

    void ServerFecParity(const uint8_t* pData);
    void FecAccumulateReceived(int64_t iSlot, int64_t iTick, std::span<const uint8_t> packetData);


5. Engine/Source/Network/Client/Client.cpp
------------------------------------------
Add case to Client::Receive() switch (line 157-190), after kServerCoordResend case (line 174):

    case PacketType::kServerFecParity:
        ServerFecParity(pData);
        break;

Modify the kServerCoordUpdate case to also feed FEC accumulation. In
Client::Receive() (line 170-171), after the ServerCoordUpdateOrResend call:

    // Note: FEC accumulation for received data packets is handled inside
    // ServerCoordUpdateOrResend, which calls FecAccumulateReceived.

Actually, cleaner: add FecAccumulateReceived call inside ServerCoordUpdateOrResend.


6. Engine/Source/Network/Client/ClientReceive.cpp
-------------------------------------------------
In Client::ServerCoordUpdateOrResend() (line 107-167):

After the mReceivedCoordUpdates push_back (line 161), before the TrackReceivedTick
call (line 164), add FEC accumulation for kServerCoordUpdate packets only (not resends):

    // FEC: accumulate received data packet for parity reconstruction
    // Only accumulate fresh updates, not resends (resends fill gaps outside FEC groups)
    if (bProcessRtt)  // bProcessRtt is true only for kServerCoordUpdate, false for resends
    {
        FecAccumulateReceived(uiSlotIndex, iTick, std::span<const uint8_t>(pData, pCursor - pData));
    }

Add new methods at end of file (before closing namespace brace, line 283):

    void Client::FecAccumulateReceived(int64_t iSlot, int64_t iTick, std::span<const uint8_t> packetData)
    {
        ClientCoordSlot& rSlot = mCoordSlots.at(iSlot);
        FecReceiveState& rFec = rSlot.fecReceive;

        // If we don't know the group size yet (set when parity arrives), just XOR in
        if (rFec.iGroupSize == 0)
        {
            // Haven't received a parity packet yet — can't do FEC. Reset on each tick.
            return;
        }

        int64_t iIndexInGroup = iTick - rFec.iGroupStartTick;
        if (iIndexInGroup < 0 || iIndexInGroup >= rFec.iGroupSize)
        {
            // Outside current group — this tick belongs to a different group, reset
            return;
        }

        // XOR into accumulator
        if (std::ssize(packetData) > std::ssize(rFec.parityAccumulator))
        {
            rFec.parityAccumulator.resize(packetData.size(), 0);
        }
        for (int64_t i = 0; i < std::ssize(packetData); ++i)
        {
            rFec.parityAccumulator.at(i) ^= packetData[i];
        }
        ++rFec.iReceivedCount;
    }

    void Client::ServerFecParity(const uint8_t* pData)
    {
        const uint8_t* pCursor = pData + 1; // Skip packet type

        uint8_t uiSlotIndex = ReadUint8(pCursor);
        uint16_t uiEpoch = ReadUint16(pCursor);
        int64_t iGroupStartTick = ReadInt64(pCursor);
        uint8_t uiGroupSize = ReadUint8(pCursor);
        int32_t iParitySize = ReadInt32(pCursor);
        const uint8_t* pParityData = pCursor;

        if (uiSlotIndex >= std::ssize(mCoordSlots))
        {
            return;
        }
        ClientCoordSlot& rSlot = mCoordSlots.at(uiSlotIndex);
        if (rSlot.eState != CoordSubscriptionState::kActive || uiEpoch != rSlot.ackState.uiEpoch)
        {
            return;
        }

        FecReceiveState& rFec = rSlot.fecReceive;

        // Check if exactly one packet is missing from this group
        int64_t iExpected = uiGroupSize;
        int64_t iReceived = rFec.iReceivedCount;

        if (rFec.iGroupStartTick != iGroupStartTick || rFec.iGroupSize != uiGroupSize)
        {
            // Group mismatch — can't reconstruct. Set up for next group.
            rFec = {};
            rFec.iGroupStartTick = iGroupStartTick + uiGroupSize;
            rFec.iGroupSize = uiGroupSize;
            return;
        }

        if (iReceived == iExpected)
        {
            // All packets received, no recovery needed
        }
        else if (iReceived == iExpected - 1)
        {
            // Exactly one missing — XOR parity with accumulated data to recover
            std::vector<uint8_t> recovered(iParitySize, 0);
            for (int64_t i = 0; i < iParitySize; ++i)
            {
                uint8_t accum = (i < std::ssize(rFec.parityAccumulator)) ? rFec.parityAccumulator.at(i) : 0;
                recovered.at(i) = pParityData[i] ^ accum;
            }

            // Find which tick is missing by checking the ACK bitfield
            for (int64_t iOffset = 0; iOffset < uiGroupSize; ++iOffset)
            {
                int64_t iTick = iGroupStartTick + iOffset;
                int64_t iBitIndex = iTick - rSlot.ackState.iAckFloor - 1;
                bool bReceived = (iTick <= rSlot.ackState.iAckFloor) ||
                                 (iBitIndex >= 0 && iBitIndex < 64 && (rSlot.ackState.uiReceivedBitfield & (1ULL << iBitIndex)));
                if (!bReceived)
                {
                    // Parse the recovered packet as a coord update
                    ServerCoordUpdateOrResend(recovered.data(), false);
                    break;
                }
            }
        }
        // else: 2+ packets missing, FEC cannot help — rely on resend system

        // Reset for next group
        rFec = {};
        rFec.iGroupStartTick = iGroupStartTick + uiGroupSize;
        rFec.iGroupSize = uiGroupSize;
    }


7. Engine/Source/Network/Server/ServerReceive.cpp
-------------------------------------------------
Adaptive FEC group sizing. In Server::ClientAckStream() (epoch/ack logic at ~lines 44-73):

After updating the ACK state, compute per-slot loss rate from the bitfield
and adjust FEC group size:

    // Adaptive FEC: adjust group size based on observed loss in the bitfield
    if (pClient->coordFecStates.at(uiSlotIndex).parityBuffer.capacity() > 0)
    {
        int64_t iBits = static_cast<int64_t>(std::popcount(uiSlotBitfield));
        int64_t iWindow = static_cast<int64_t>(std::bit_width(uiSlotBitfield));
        if (iWindow > 0)
        {
            float fLossRate = 1.0f - static_cast<float>(iBits) / static_cast<float>(iWindow);
            FecSlotState& rFec = pClient->coordFecStates.at(uiSlotIndex);
            if (fLossRate > kfFecShrinkLossThreshold && rFec.iGroupSize > kiFecGroupSizeMin)
            {
                rFec.iGroupSize = std::max(kiFecGroupSizeMin, rFec.iGroupSize / 2);
            }
            else if (fLossRate < kfFecGrowLossThreshold && rFec.iGroupSize < kiFecGroupSizeMax)
            {
                rFec.iGroupSize = std::min(kiFecGroupSizeMax, rFec.iGroupSize * 2);
            }
        }
    }

Note: The FecSlotState struct must be forward-declared or the header included.
Since Server.h already contains ClientConnection with the new coordFecStates
vector, and ServerReceive.cpp already includes Server.h, no new includes needed.


DESIGN NOTES
=============

1. FEC parity packets use the same unreliable coord slot channel as data packets
   (NetworkManager::CoordSlotUnreliable). This means they share ordering with
   data packets on that slot, which is fine since ENet unreliable channels have
   no ordering guarantees anyway.

2. The FEC accumulator on the client XORs raw packet bytes (including the header).
   This means the recovered packet will have a valid header and can be fed directly
   to ServerCoordUpdateOrResend(). The packet type byte will be kServerCoordUpdate
   (recovered via XOR), so the parse path is the same.

3. FEC only protects kServerCoordUpdate packets. Resends (kServerCoordResend) and
   full state (kServerCoordFullState) are not FEC-protected because:
   - Resends are already the fallback for lost packets
   - Full state uses reliable delivery

4. When a client re-subscribes (epoch change), FEC state is reset via FreeSlot()
   which clears coordFecStates for that slot.

5. The parity buffer in FecSlotState uses a std::vector<uint8_t> which is a heap
   allocation. This is acceptable because it is per-client-per-slot and reused
   across groups. Add ScopedSuppressAllocationTracking where the vector is first
   resized, and a "// Heap:" comment.

6. Client-side FEC state (FecReceiveState) also needs its parityAccumulator vector
   guarded with ScopedSuppressAllocationTracking and "// Heap:" comment.

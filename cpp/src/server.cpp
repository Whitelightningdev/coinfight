#include <iostream>
#include <boost/shared_ptr.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <filesystem>
#include <vector>
#include <string>
#include "cmds.h"
#include "engine.h"
#include "config.h"
#include "packets.h"
#include "sigWrapper.h"
#include "events.h"

using namespace std;
using namespace boost::asio::ip;

class ClientChannel;

vector<ClientChannel *> clientChannels;

class Listener
{
    boost::asio::io_service &ioService;
    tcp::acceptor acceptor;

public:
    Listener(boost::asio::io_service &ioService_)
        : ioService(ioService_), acceptor(ioService_, tcp::endpoint(tcp::v4(), 8473))
    {
    }
    void startAccept()
    {
        boost::shared_ptr<tcp::socket> socket(new tcp::socket(ioService));

        acceptor.async_accept(*socket, boost::bind(&Listener::handleAccept, this, socket, boost::asio::placeholders::error));
    }
    void handleAccept(boost::shared_ptr<tcp::socket> socket, const boost::system::error_code &error);
};

Game game;
bool adminRoleTaken(false);

void testHandler(const boost::system::error_code &error, size_t numSent)
{
    if (!error)
    {
        cout << "woo! " << numSent << endl;
    }
    else
    {
        cout << "boooooo" << endl;
    }
}

void clearVchAndPackResyncPacket(vch *dest)
{
    dest->clear();
    game.pack(dest);

    vch prepended;
    packToVch(&prepended, "C", PACKET_RESYNC_CHAR);
    packToVch(&prepended, "Q", (uint64_t)(dest->size()));

    dest->insert(dest->begin(), prepended.begin(), prepended.end());
}
void clearVchAndPackFrameCmdsPacket(vch *dest, FrameEventsPacket fcp)
{
    dest->clear();

    fcp.pack(dest);

    vch prepended;
    packToVch(&prepended, "C", PACKET_FRAMECMDS_CHAR);
    packToVch(&prepended, "Q", (uint64_t)dest->size());

    dest->insert(dest->begin(), prepended.begin(), prepended.end());
}

vector<boost::shared_ptr<AuthdCmd>> pendingCmds;

class ClientChannel
{
    boost::asio::io_service &ioService;
    boost::shared_ptr<tcp::socket> socket;

    vector<vch *> packetsToSend;
    bool sending;

    vch receivedBytes;
    boost::asio::streambuf receivedSig;

    string sentChallenge;

    string genRandomString(int len)
    {
        // hacky, untested, probably insecure!! Only good for the hackathon and a demo.
        static const char alphanum[] =
            "0123456789"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz";
        std::string s;
        s.reserve(len);

        for (int i = 0; i < len; ++i) {
            int randChoice = (int)((double)rand() / ((double)RAND_MAX + 1) * (sizeof(alphanum) - 1));
            s += alphanum[randChoice];
        }
        
        return s;
    }

public:
    enum State {
        DoingHandshake,
        ReadyForFirstSync,
        UpToDate,
        Closed
    } state;
    string connectionAuthdUserAddress;
    ClientChannel(boost::asio::io_service &ioService_, boost::shared_ptr<tcp::socket> socket_)
        : ioService(ioService_), socket(socket_), receivedSig(150)
    {
        state = DoingHandshake;
        sending = false;
    }

    void startHandshakeAsync()
    {
        generateAndSendSigChallenge();
        receiveSigAsync(); // also kicks off receiving loop
    }

    void generateAndSendSigChallenge()
    {
        string challenge = genRandomString(50);

        boost::asio::write(*socket, boost::asio::buffer(challenge));

        cout << "sent challenge" << endl;

        sentChallenge = challenge;
    }

    void receiveSigAsync()
    {
        boost::asio::async_read_until(*socket,
                   receivedSig,
                   '\n',
                   boost::bind(&ClientChannel::sigReceived,
                               this,
                               boost::asio::placeholders::error,
                               boost::asio::placeholders::bytes_transferred));
    }

    void sigReceived(const boost::system::error_code &error, size_t transferred)
    {
        cout << "sig received" << endl;
        if (error)
        {
            cout << "Error receiving sig from " << connectionAuthdUserAddress << ". Kicking." << endl;
            state = Closed;
        }
        else
        {
            // receivedSig now has sig
            // But leave out the trailing \n leftover
            string sig(boost::asio::buffer_cast<const char*>(receivedSig.data()), receivedSig.size() - 1);

            if (sig == string("admin"))
            {
                if (!adminRoleTaken)
                {
                    adminRoleTaken = true;
                    connectionAuthdUserAddress = string("0xBB5eb03535FA2bCFe9FE3BBb0F9cC48385818d92");
                }
            }
            else
            {
                // now have sig and sentChallenge as strings.
                string error;
                if (auto maybeRecoveredAddress = signedMsgToAddress(sentChallenge, sig, &error))
                {
                    connectionAuthdUserAddress = *maybeRecoveredAddress;
                }
                else
                {
                    cout << "Error recovering address from connection. Kicking." << endl << "Here's the Python error message:" << endl;
                    cout << error << endl;
                    state = Closed;
                    return;
                }
            }

            cout << "Player authenticated and connected." << endl;

            // should really return a fail/success code here. On fail client just hangs atm.
            boost::asio::write(*socket, boost::asio::buffer(connectionAuthdUserAddress));

            state = ReadyForFirstSync;
            startReceivingLoop();
        }
    }

    void sendResyncPacket()
    {
        packetsToSend.push_back(new vch);

        clearVchAndPackResyncPacket(packetsToSend.back());

        sendNextPacketIfNotBusy();
    }

    void sendFrameCmdsPacket(FrameEventsPacket fcp)
    {
        packetsToSend.push_back(new vch);

        clearVchAndPackFrameCmdsPacket(packetsToSend.back(), fcp);

        sendNextPacketIfNotBusy();
    }

    void startReceivingLoop()
    {
        clearVchAndReceiveNextCmd();
    }

    void sendNextPacketIfNotBusy()
    {
        if (!sending && packetsToSend.size() > 0)
        {
            sending = true;
            boost::asio::async_write(*socket,
                                     boost::asio::buffer(*(packetsToSend[0])),
                                     boost::bind(&ClientChannel::wrapUpSendingPacket,
                                                 this,
                                                 packetsToSend[0],
                                                 boost::asio::placeholders::error,
                                                 boost::asio::placeholders::bytes_transferred));
        }
    }

    void wrapUpSendingPacket(vch *sourceToDispose, const boost::system::error_code &error, size_t bytes_transferred)
    {
        if (error)
        {
            cout << "Error sending packet to " << connectionAuthdUserAddress << ". Kicking." << endl;
            state = Closed;
        }
        else
        {
            assert(packetsToSend[0] == sourceToDispose);

            delete packetsToSend[0];
            packetsToSend.erase(packetsToSend.begin());

            sending = false;

            sendNextPacketIfNotBusy();
        }
    }

    void clearVchAndReceiveNextCmd()
    {
        receivedBytes = vch(2);

        async_read(*socket,
                   boost::asio::buffer(receivedBytes),
                   boost::bind(&ClientChannel::sizeReceived,
                               this,
                               boost::asio::placeholders::error,
                               boost::asio::placeholders::bytes_transferred));
    }
    void sizeReceived(const boost::system::error_code &error, size_t transferred)
    {
        if (error)
        {
            cout << "Error receiving cmd size from " << connectionAuthdUserAddress << ": " << error.message() << endl << "Kicking." << endl;
            state = Closed;
        }
        else
        {
            vchIter place = receivedBytes.begin();

            uint16_t size;
            unpackFromIter(place, "H", &size);

            clearVchAndReceiveCmdBody(size);
        }
    }
    void clearVchAndReceiveCmdBody(uint16_t size)
    {
        receivedBytes = vch(size);

        async_read(*socket,
                   boost::asio::buffer(receivedBytes),
                   boost::bind(&ClientChannel::cmdBodyReceived,
                               this,
                               boost::asio::placeholders::error,
                               boost::asio::placeholders::bytes_transferred));
    }
    void cmdBodyReceived(const boost::system::error_code &error, size_t transferred)
    {
        if (error)
        {
            cout << "Error receiving cmd body from " << connectionAuthdUserAddress << ". Kicking." << endl;
            state = Closed;
        }
        else
        {
            vchIter place = receivedBytes.begin();

            boost::shared_ptr<Cmd> cmd = unpackFullCmdAndMoveIter(&place);
            boost::shared_ptr<AuthdCmd> authdCmd = boost::shared_ptr<AuthdCmd>(new AuthdCmd(cmd, this->connectionAuthdUserAddress));

            pendingCmds.push_back(authdCmd);

            clearVchAndReceiveNextCmd();
        }
    }
};

void Listener::handleAccept(boost::shared_ptr<tcp::socket> socket, const boost::system::error_code &error)
{
    if (error)
    {
        throw("Listener error accepting:" + error.value());
    }
    else
    {
        cout << "client connected!" << endl;
        ClientChannel *clientChannel = new ClientChannel(ioService, socket);
        clientChannel->startHandshakeAsync();
        
        clientChannels.push_back(clientChannel);

    }
    startAccept();
}

struct WithdrawEvent
{
    string userAddress;
    coinsInt amountInCoins;
    WithdrawEvent(string userAddress, coinsInt amountInCoins)
        : userAddress(userAddress), amountInCoins(amountInCoins) {}
    boost::shared_ptr<Event> toEventSharedPtr()
    {
        return boost::shared_ptr<Event>(new BalanceUpdateEvent(userAddress, amountInCoins, false));
    }
};

void actuateWithdrawal(string userAddress, coinsInt amount)
{
    string weiString = coinsIntToWeiDepositString(amount);
    string writeData = userAddress + " " + weiString;

    string filename = to_string(time(0)) + "-" + to_string(clock());
    ofstream withdrawDescriptorFile(filename);
    withdrawDescriptorFile << writeData;
    withdrawDescriptorFile.close();

    filesystem::rename(filename, "./accounting/pending_withdrawals/" + filename);
}

vector<boost::shared_ptr<Event>> pollPendingDepositsAndHoneypotEvents()
{
    vector<boost::shared_ptr<Event>> events;

    boost::filesystem::path accountingDirPath("./accounting/pending_deposits/");
    boost::filesystem::directory_iterator directoryEndIter; // default constructor makes it an end_iter

    for (boost::filesystem::directory_iterator dirIter(accountingDirPath); dirIter != directoryEndIter; dirIter++)
    {
        if (boost::filesystem::is_regular_file(dirIter->path())) {
            string depositFilePath = dirIter->path().string();
            std::ifstream depositFile(depositFilePath);
            string depositData;

            if (depositFile.is_open())
            {
                while (depositFile)
                {
                    string depositCmdData;
                    getline(depositFile, depositCmdData);
                    if (depositCmdData.length() == 0)
                        continue;

                    vector<string> splitted;
                    boost::split(splitted, depositCmdData, boost::is_any_of(" "));
                    string userAddressOrHoneypotString = splitted[0];
                    string depositWeiString = splitted[1];
                    coinsInt depositInCoins = weiDepositStringToCoinsInt(depositWeiString);

                    if (userAddressOrHoneypotString == "honeypot")
                    {
                        events.push_back(boost::shared_ptr<Event>(new HoneypotAddedEvent(depositInCoins)));
                    }
                    else
                    {
                        events.push_back(boost::shared_ptr<Event>(new BalanceUpdateEvent(userAddressOrHoneypotString, depositInCoins, true)));
                    }
                }
            }
            else
            {
                throw runtime_error("Couldn't open a deposit file...\n");
            }
            depositFile.close();
            // delete the file, having processed it
            boost::filesystem::remove(dirIter->path());
        }
    }

    return events;
}

int main(int argc, char *argv[])
{
    srand(time(0));

    boost::asio::io_service io_service;

    Listener listener(io_service);
    listener.startAccept();

    // server will scan this directory for pending deposits (supplied by py/balance_tracker.py)
    boost::filesystem::path accountingDirPath("./accounting/pending_deposits/");
    boost::filesystem::directory_iterator directoryEndIter; // default constructor makes it an end_iter

    chrono::time_point<chrono::system_clock, chrono::duration<double>> nextFrameStart(chrono::system_clock::now());

    vector<WithdrawEvent> pendingWithdrawEvents;
    
    while (true)
    {
        // poll io_service, which will populate pendingCmds with anything the ClientChannels have received
        io_service.poll();

        // rate limit iteration to a maximum of SEC_PER_FRAME
        chrono::time_point<chrono::system_clock, chrono::duration<double>> now(chrono::system_clock::now());
        if (now < nextFrameStart)
            continue;
        nextFrameStart += ONE_FRAME;

        // let's count up events
        vector<boost::shared_ptr<Event>> pendingEvents;

        // did we see any withdrawals last loop?
        // If so, actuate and queue for in-game processing
        for (uint i=0; i<pendingWithdrawEvents.size(); i++)
        {
            // just make sure again the math works out
            if (pendingWithdrawEvents[i].amountInCoins > game.players[game.playerAddressToIdOrNegativeOne(pendingWithdrawEvents[i].userAddress)].credit.getInt())
            {
                cout << "Somehow an invalid withdrawal event was about to get processed..." << endl;
            }
            else
            {
                actuateWithdrawal(pendingWithdrawEvents[i].userAddress, pendingWithdrawEvents[i].amountInCoins);
                pendingEvents.push_back(pendingWithdrawEvents[i].toEventSharedPtr());
            }
        }
        pendingWithdrawEvents.clear();

        // scan for any pending deposits or honeypotAdd events
        vector<boost::shared_ptr<Event>> depositAndHoneypotEvents = pollPendingDepositsAndHoneypotEvents();
        pendingEvents.insert(pendingEvents.end(), depositAndHoneypotEvents.begin(), depositAndHoneypotEvents.end());

        // build FrameEventsPacket for this frame
        // includes all cmds we've received from clients since last time and all new events
        FrameEventsPacket fcp(game.frame, pendingCmds, pendingEvents);

        // send the packet out to all clients
        for (unsigned int i = 0; i < clientChannels.size(); i++)
        {
            switch (clientChannels[i]->state)
            {
                case ClientChannel::DoingHandshake:
                    break;

                case ClientChannel::ReadyForFirstSync:
                    clientChannels[i]->sendResyncPacket();
                    clientChannels[i]->sendFrameCmdsPacket(fcp);

                    clientChannels[i]->state = ClientChannel::UpToDate;
                    break;

                case ClientChannel::UpToDate:
                    clientChannels[i]->sendFrameCmdsPacket(fcp);
                    break;
                
                case ClientChannel::Closed:
                    clientChannels.erase(clientChannels.begin()+i);
                    i--;
                    break;
            }
        }

        // execute all events
        for (unsigned int i = 0; i < pendingEvents.size(); i++)
        {
            pendingEvents[i]->execute(&game);
        }
        pendingEvents.clear();

        // execute all cmds on server-side game
        for (unsigned int i = 0; i < pendingCmds.size(); i++)
        {
            auto cmd = pendingCmds[i]->cmd;
            if (auto unitCmd = boost::dynamic_pointer_cast<UnitCmd, Cmd>(cmd))
            {
                unitCmd->executeAsPlayer(&game, pendingCmds[i]->playerAddress);
            }
            else if (auto spawnBeaconCmd = boost::dynamic_pointer_cast<SpawnBeaconCmd, Cmd>(cmd))
            {
                spawnBeaconCmd->executeAsPlayer(&game, pendingCmds[i]->playerAddress);
            }
            else if (auto withdrawCmd = boost::dynamic_pointer_cast<WithdrawCmd, Cmd>(pendingCmds[i]->cmd))
            {
                int playerId = game.playerAddressToIdOrNegativeOne(pendingCmds[i]->playerAddress);
                if (playerId < 0)
                {
                    cout << "Woah, getting a negative playerId when processing a withdraw cmd..." << endl;
                    continue;
                }
                // if 0, interpret this as "all"
                coinsInt withdrawSpecified = withdrawCmd->amount > 0 ? withdrawCmd->amount : game.players[playerId].credit.getInt();
                coinsInt amountToWithdraw = min(withdrawSpecified, game.players[playerId].credit.getInt());

                pendingWithdrawEvents.push_back(WithdrawEvent(pendingCmds[i]->playerAddress, amountToWithdraw));
            }
            else
            {
                cout << "Woah, I don't know how to handle that cmd as a server!" << endl;
            }
        }
        pendingCmds.clear();

        game.iterate();
    }

    cout << "oohhhhh Logan you done did it this time" << endl;

    return 0;
}
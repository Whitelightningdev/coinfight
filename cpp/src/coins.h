#include <algorithm>

#ifndef COINS_H
#define COINS_H

const int CREDIT_PER_DOLLAR_EXPONENT = 3; // credit = dollar * 10^X
const int WEI_PER_DOLLAR_EXPONENT = 18; // using xDai, so wei = dollar * 10^18

using namespace std;
using vch = vector<unsigned char>;
using vchIter = vector<unsigned char>::iterator;
using coinsInt = unsigned long;

extern const coinsInt MAX_COINS;

coinsInt weiDepositStringToCoinsInt(string weiString);

class Coins
{
private:
    coinsInt heldAmount;
    coinsInt deductUpTo(coinsInt);
    coinsInt addUpTo(coinsInt);
    bool tryDeduct(coinsInt);
    bool tryAdd(coinsInt);
public:
    coinsInt max;
    coinsInt getInt();
    // sf::String getDollarString();
    Coins(coinsInt);
    Coins(vchIter*);
    coinsInt getSpaceLeft();
    bool createMoreByFiat(coinsInt);
    bool destroySomeByFiat(coinsInt);
    coinsInt transferUpTo(coinsInt, Coins*);
    bool tryTransfer(coinsInt, Coins*);
    void pack(vch*);
    void unpackAndMoveIter(vchIter*);
};

#endif // COINS_H
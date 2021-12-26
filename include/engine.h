#include <iostream>
#include <boost/shared_ptr.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <vector>
#include <string>
#include <pthread.h>
#include "myvectors.h"
#include "vchpack.h"
#include "common.h"

#ifndef ENGINE_H
#define ENGINE_H

using namespace std;

using vch = vector<unsigned char>;
using vchIter = vector<unsigned char>::iterator;
using EntityRef = uint16_t;

class Game;

class Entity
{
public:
    Game *game;
    bool dead;
    EntityRef id;
    vector2f pos;

    virtual unsigned char typechar();
    virtual string getTypeName();
    virtual void pack(vch *dest);
    virtual void unpackAndMoveIter(vchIter *iter, Game &game);
    virtual void go();
    void die();

    bool collidesWithPoint(vector2f);

    void packEntity(vch *destVch);
    void unpackEntityAndMoveIter(vchIter *iter);
    Entity(Game *game, EntityRef id, vector2f pos);
    Entity(Game *game, EntityRef id, vchIter *iter);

    vector2f getPos();
};

unsigned char getMaybeNullEntityTypechar(boost::shared_ptr<Entity>);

vector<EntityRef> entityPointersToRefs(vector<boost::shared_ptr<Entity>>);

boost::shared_ptr<Entity> unpackFullEntityAndMoveIter(vchIter *iter, unsigned char typechar, Game *game, EntityRef id);

class Target
{
private:
    vector2f pointTarget;
    EntityRef entityTarget;
public:
    enum Type
    {
        PointTarget,
        EntityTarget
    } type;

    void pack(vch *dest);
    void unpackAndMoveIter(vchIter *iter);

    Target(vector2f);
    Target(EntityRef);
    Target(vchIter *iter);

    optional<vector2f> getPoint(Game*);
    optional<EntityRef> castToEntityRef();
    optional<vector2f> castToPoint();
    boost::shared_ptr<Entity> castToEntityPtr(Game*);
};

class Game
{
public:
    float playerCredit;
    uint64_t frame;
    vector<boost::shared_ptr<Entity>> entities;

    boost::shared_ptr<Entity> entityRefToPtr(EntityRef);
    EntityRef getNextEntityRef();

    void pack(vch *dest);
    void unpackAndMoveIter(vchIter *iter);

    Game();
    Game(vchIter *);

    void reassignEntityGamePointers();

    void testInit();
    void iterate();
};

class GoldPile : public Entity
{
public:
    uint32_t amount;

    void pack(vch *destVch);
    void unpackAndMoveIter(vchIter *iter);
    GoldPile(Game *, EntityRef, vector2f, uint32_t);
    GoldPile(Game *, EntityRef, vchIter *);

    unsigned int tryDeductAmount(unsigned int);
    unsigned int tryAddAmount(unsigned int);

    unsigned char typechar();
    string getTypeName();
    void go();
};

class Unit : public Entity
{
protected:
    float builtAmount;

public:
    virtual float getCreditCost();

    void packUnit(vch *destVch);
    void unpackUnitAndMoveIter(vchIter *iter);
    Unit(Game *game, EntityRef id, vector2f pos);
    Unit(Game *game, EntityRef id, vchIter *iter);

    float build(float attemptedAmount);
    float getBuiltAmount();
    bool isActive();
};

class Building : public Unit
{
public:
    void packBuilding(vch *destVch);
    void unpackBuildingAndMoveIter(vchIter *iter);

    Building(Game *game, EntityRef id, vector2f pos);
    Building(Game *game, EntityRef id, vchIter *iter);
};

class MobileUnit : public Unit
{
private:
    Target target;
    float targetRange;

    void moveTowardPoint(vector2f, float);

public:
    virtual float getSpeed();
    virtual float getRange();

    void setTarget(Target _target, float range);

    void packMobileUnit(vch *destVch);
    void unpackMobileUnitAndMoveIter(vchIter *iter);

    void mobileUnitGo();

    void cmdMove(vector2f target);

    MobileUnit(Game *game, EntityRef id, vector2f pos);
    MobileUnit(Game *game, EntityRef id, vchIter *iter);
};

class Prime : public MobileUnit
{
public:
    Target target;
    uint32_t heldCredit;

    enum State
    {
        Idle,
        PickupGold,
        PutdownGold
    } state;

    void pack(vch *dest);
    void unpackAndMoveIter(vchIter *iter);

    Prime(Game *game, EntityRef id, vector2f pos);
    Prime(Game *game, EntityRef id, vchIter *iter);

    void cmdPickup(EntityRef);
    void cmdPutdown(Target);

    float getSpeed();
    float getRange();

    unsigned char typechar();
    string getTypeName();
    float getCreditCost();
    void go();
};

class Gateway : public Building
{
private:
    void iterateSpawning();

public:
    enum State
    {
        Idle,
        Spawning,
        Reclaiming,
        ReclaimingSelf
    } state;
    EntityRef spawningPrimeId;
    boost::shared_ptr<Prime> spawningPrime();

    void pack(vch *dest);
    void unpackAndMoveIter(vchIter *iter);

    Gateway(Game *game, EntityRef id, vector2f pos, bool alreadyCompleted);
    Gateway(Game *game, EntityRef id, vchIter *iter);

    unsigned char typechar();
    string getTypeName();
    float getCreditCost();
    void go();

    void startSpawningPrime(vector2f primePos);
};

void packFrameCmdsPacket(vch *dest, uint64_t frame);

#endif // ENGINE_H
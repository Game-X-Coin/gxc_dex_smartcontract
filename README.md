# gxc_dex_smartcontract


gxc smart contract (can running in eos) for decentralized exchange.


## table

* orders

save infomration for orders.



## functions

* createorder

Create order for dex. assets are transfered to dex account.

* cancelorder

Cancel order which was not fully fulfilled. remain deposit assets are refunded.

* fufillorder

Match maker and taker order. min(remain_asset) will transfered to each owner. Deposit assets that have bean fully fulfilled are refunded to owner.


## build

./build.sh


## deploy
* cleos create account eosio dex owner_key active_key
* import active key to wallet
* cleos set contract dex ../dex
* cleos set account permission dex active '{"threshold": 1,"keys": [{"key": "active_key","weight": 1}],"accounts": [{"permission":{"actor":"dex","permission":"eosio.code"},"weight":1}]}' owner -p dex

## usage

1. Users who want to use this contracts must grant permission to dex@dosio.code using 

```
cleos set account permission account active '{"threshold": 1,"keys": [{"key": "active_key","weight": 1}],"accounts": [{"permission":{"actor":"dex","permission":"eosio.code"},"weight":1}]}' owner -p account
```

2. make order

```
cleos push action dex makeorder '[ "alice", 0, "100.0000 GXQ", 0.1 ]' -p alice
cleos push action dex makeorder '[ "bob", 1, "100.0000 GXQ", 0.09 ]' -p bob
```

3. fulfill order

```
cleos push action dex fulfillorder '[6082100345526222745, 8604223846098659695]' -p bob
```

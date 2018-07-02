#include <eosiolib/eosio.hpp>
#include <eosiolib/asset.hpp>
#include <eosiolib/currency.hpp>

#include <eosiolib/crypto.h>
#include "../eosio.token/eosio.token.hpp"

using namespace eosio;

class dex : public eosio::contract {
  public:
      using contract::contract;
      dex(account_name self)
      : eosio::contract(self),
        orders(_self, _self)
      {}
      /// @abi table orders
      struct order {
        uint64_t id;
        account_name owner;
        symbol_type symbol;
        uint64_t total_amount;
        uint64_t fulfilled_amount;
        asset deposit_remain_asset;
        double price; //gxc trade price. ex) 0.03 means 0.03gxc
        uint8_t type; // 0-> buy, 1-> sell
        uint8_t status; //0-> init ,1 -> complete 2-> cancel
        time_t t = 0;
        uint64_t primary_key() const { return id; }
        uint64_t remain_amount() const { return total_amount - fulfilled_amount;}

      };
      struct account {
        asset balance;
        uint64_t primary_key()const { return balance.symbol.name(); }
      };

      typedef eosio::multi_index<N(accounts), account> accounts;
      typedef eosio::multi_index<N(orders), order> order_index;
      order_index orders;
      /// @abi action
      void makeorder(account_name from, uint8_t type, asset quantity, double price) {
        // print("Opening ", from, " ", " for ", quantity.symbol.name(), quantity.amount, " - ");
        eosio_assert(quantity.symbol.is_valid(), "invalid symbol name" );
        eosio_assert(quantity.amount > 0, "invalid token quantity");
        eosio_assert(is_account(from), "invalid from!");
        
        require_auth(from);
        /*eosio_assert(enoughMoney(from, quantity), "not enough money!"); */
        time_t t = now();
        auto order_id = calcOrderId(from, quantity, t);
        asset deposit_asset = quantity;
        if(0 == type) deposit_asset = asset(price * quantity.amount, string_to_symbol(4, "GXC"));
        // asset& deposit_asset = int8_t(0) == type ? asset(price * quantity.amount ,symbol_name("GXC")) : quantity;
        //orders order( _self, N(dex.code) );
        
        orders.emplace(from, [&](auto& a) {
          a.id = order_id;
          a.owner = from;
          a.fulfilled_amount = 0;
          a.total_amount = quantity.amount;
          a.price = price;
          a.symbol = quantity.symbol;
          a.type = type;
          a.deposit_remain_asset = deposit_asset;
          a.t = t;
          a.status = 0;
        });
        action(
          permission_level{ from, N(active) },
          N(eosio.token), N(transfer),
          std::make_tuple(from, _self, deposit_asset, std::string(""))
        ).send();
        print("order_id:", order_id);
        /*accounts.adjust_balance( from, quantity, "deposit" );
        orders orderstable(
        orders.emplace( from, [&]( auto& s ) {
            s.id = 1;
            s.type = type;
            s.symbol = quantity.symbol;
            s.owner = from;
            s.total_amount = quantity.quantity;
        });*/
      }
      
      /// @abi action
      void cancelorder(uint64_t order_id) {
        const auto& order = orders.get(order_id, "order doesn't exist");
        require_auth(order.owner);
        auto deposit_remain_asset = order.deposit_remain_asset;
        // asset amount = order.type == 0 ? asset(order.price * remain_amount, string_to_symbol(4, "GXC")) : asset(remain_amount, order.symbol); 
        print("remain_asset", deposit_remain_asset);
        action(
          permission_level{ _self, N(active) },
          N(eosio.token), N(transfer),
          std::make_tuple(_self, order.owner, deposit_remain_asset, std::string(""))
        ).send();
        orders.modify(order, _self, [&](auto & acnt) {
          acnt.status = 2;
          acnt.deposit_remain_asset.amount = 0;
        });
        print("order_id:", order_id);
      }

      /// @abi action
      void fulfillorder(uint64_t maker_order_id, uint64_t taker_order_id) {
        const auto& maker_order = orders.get(maker_order_id, "maker order doesn't exist");
        const auto& taker_order = orders.get(taker_order_id, "taker order doesn't exist");
        require_auth(taker_order.owner);
        if(taker_order.type == 0) { //buy
          eosio_assert(maker_order.type == 1, "maker type is not match");
          eosio_assert(taker_order.price >= maker_order.price, "when taker buy, taker rate would be larget than maker_order");
        } else { // taker sell
          eosio_assert(maker_order.type == 0, "maker type is not match");
          eosio_assert(taker_order.price <= maker_order.price, "when taker sell, taker rate would be smaller than maker_order");
        }
        double price = maker_order.price;
        auto taker_amount = taker_order.remain_amount(); // gxc amount
        auto maker_amount = maker_order.remain_amount();
        auto amount = taker_amount < maker_amount ? taker_amount : maker_amount;
        uint64_t gxc_amount = uint64_t(amount * price);
        auto buyer_get_amount = asset(amount, taker_order.symbol);
        auto seller_get_amount = asset(gxc_amount, string_to_symbol(4, "GXC"));  
        auto maker_get_amount = maker_order.type == 0 ? buyer_get_amount : seller_get_amount;
        auto taker_get_amount = taker_order.type == 0 ? buyer_get_amount : seller_get_amount;
        /*update_and_transfer_order(taker_order, taker_get_amount, maker_get_amount);
        update_and_transfer_order(maker_order, maker_get_amount, taker_get_amount);*/
        uint8_t taker_status = taker_order.total_amount == (taker_order.fulfilled_amount + taker_get_amount.amount) ? 1 : 0;
        asset taker_withdraw_asset = taker_order.deposit_remain_asset - maker_get_amount;
        orders.modify(taker_order, _self, [&](auto & acnt) {
          acnt.fulfilled_amount += taker_get_amount.amount;
          acnt.status = taker_status;
          if(taker_status == 1){
            acnt.deposit_remain_asset.amount = 0;
          } else {
            acnt.deposit_remain_asset -= maker_get_amount;
          }
        });

        action(
          permission_level{ _self, N(active) },
          N(eosio.token), N(transfer),
          std::make_tuple(_self, taker_order.owner, taker_get_amount, std::string(""))
        ).send();
        if(taker_status == 1 && taker_withdraw_asset.max_amount > 0 ) {
          action(
            permission_level{ _self, N(active) },
            N(eosio.token), N(transfer),
            std::make_tuple(_self, taker_order.owner, taker_withdraw_asset, std::string(""))
          ).send();
        }

        uint8_t maker_status = maker_order.total_amount == (maker_order.fulfilled_amount + maker_get_amount.amount) ? 1 : 0;
        asset maker_withdraw_asset = maker_order.deposit_remain_asset - taker_get_amount;
        orders.modify(maker_order, _self, [&](auto & acnt) {
          acnt.fulfilled_amount += maker_get_amount.amount;
          acnt.status = maker_status;
          if(maker_status == 1){
            acnt.deposit_remain_asset.amount = 0;
          } else {
            acnt.deposit_remain_asset -= taker_get_amount;
          }
        });

        action(
          permission_level{ _self, N(active) },
          N(eosio.token), N(transfer),
          std::make_tuple(_self, maker_order.owner, maker_get_amount, std::string(""))
        ).send();
        if(maker_status == 1 && maker_withdraw_asset.amount > 0) {
          action(
            permission_level{ _self, N(active) },
            N(eosio.token), N(transfer),
            std::make_tuple(_self, maker_order.owner, maker_withdraw_asset, std::string(""))
          ).send();
        }

        print("fulfilled_amount:", amount, "price:", price);
      }

      /// @abi action 
      void hi( account_name user ) {
         print( "Hello, ", name{user} );
         print( "Hello, ", _self );
      }
  private:
    uint64_t calcOrderId(account_name from,asset quantity, time_t t)
    {
      auto hash = calcChannelHash(from, quantity, t);
      return reinterpret_cast<uint64_t*>(hash.hash)[0];
    }
    checksum256 calcChannelHash(account_name from,asset quantity, time_t t)
    {
      std::string hash_source = std::to_string(from)
        + std::to_string(quantity.amount)
        + std::to_string(quantity.symbol.name())
        + std::to_string(t);
      
      checksum256 id_hash;
      sha256(const_cast<char*>(hash_source.c_str()), hash_source.length(), &id_hash);
      return id_hash;
    }
    void update_and_transfer_order(order order, asset get_amount, asset subtract_amount) {
      uint8_t status = order.total_amount == (order.fulfilled_amount + get_amount.amount) ? 1 : 0;
      asset withdraw_asset = order.deposit_remain_asset - subtract_amount;
      orders.modify(order, _self, [&](auto & acnt) {
        acnt.fulfilled_amount += get_amount.amount;
        acnt.status = status;
        if(status == 1){
          acnt.deposit_remain_asset.amount = 0;
        } else {
          acnt.deposit_remain_asset -= subtract_amount;
        }
      });

      action(
        permission_level{ _self, N(active) },
        N(eosio.token), N(transfer),
        std::make_tuple(_self, order.owner, get_amount, std::string(""))
      ).send();
      if(status == 1) {
        action(
          permission_level{ _self, N(active) },
          N(eosio.token), N(transfer),
          std::make_tuple(_self, order.owner, withdraw_asset, std::string(""))
        ).send();
      }
    }
};

EOSIO_ABI( dex, (makeorder)(cancelorder)(fulfillorder)(hi))
#include <atomicpacks.hpp>

/**
* Sells RAM bytes from the from account's balance and transfers the proceeds to that account
* @required_auth from
*/
ACTION atomicpacks::withdrawram(
    name from,
    int64_t bytes
) {
    require_auth(from);

    decrease_ram_balance(from, bytes, "The account does not have a sufficient ram balance");

    asset payout = ram::get_sell_ram_quantity(bytes);

    action(
        permission_level{get_self(), name("active")},
        name("eosio"),
        name("sellram"),
        std::make_tuple(
            get_self(),
            bytes
        )
    ).send();

    action(
        permission_level{get_self(), name("active")},
        name("eosio.token"),
        name("transfer"),
        std::make_tuple(
            get_self(),
            from,
            payout,
            string("Sold RAM")
        )
    ).send();
}


/**
* Transfers RAM from an account balance to a collection balance
* @required_auth from_account
*/
ACTION atomicpacks::addcolram(
    name from_account,
    name to_collection_name,
    int64_t bytes
) {
    require_auth(from_account);

    check(bytes > 0, "bytes must be positive");

    atomicassets::collections.require_find(to_collection_name.value,
        "No collection with this name exists");

    decrease_ram_balance(from_account, bytes,
        "The account does not have enough RAM balance");
    increase_collection_ram_balance(to_collection_name, bytes);
}


/**
* Transfers RAM from a collection balance to an account balance
* @required_auth authorized_account, who must be authorized within the collection
*/
ACTION atomicpacks::removecolram(
    name authorized_account,
    name from_collection_name,
    name to_account,
    int64_t bytes
) {
    require_auth(authorized_account);
    check_has_collection_auth(authorized_account, from_collection_name);

    check(is_account(to_account), "to_account does not exist");

    check(bytes > 0, "bytes must be positive");

    decrease_collection_ram_balance(from_collection_name, bytes,
        "The collection does not have enough RAM balance");
    increase_ram_balance(to_account, bytes);
}


/**
* This function is called when the contract receives an eosio.token transfer
* Any core token transferred to the contract is automatically converted to RAM and added
* to the sender's RAM balance
*/
void atomicpacks::receive_token_transfer(
    name from,
    name to,
    asset quantity,
    string memo
) {
    if (to != get_self() || from == name("eosio.ram")) {
        return;
    }

    check(memo == "deposit ram", "Invalid memo");

    increase_ram_balance(from, ram::get_purchase_ram_bytes(quantity));

    action(
        permission_level{get_self(), name("active")},
        name("eosio"),
        name("buyram"),
        std::make_tuple(
            get_self(),
            get_self(),
            quantity
        )
    ).send();
}


/**
* Internal function to increase the ram balance of an account
*/
void atomicpacks::increase_ram_balance(
    name account,
    int64_t bytes
) {
    check(bytes > 0, "increase balance bytes must be positive");

    auto itr = rambalances.find(account.value);
    if (itr == rambalances.end()) {
        check(bytes >= 128, "Must inrease the ram balance by at least 128 to pay for the table entry");
        rambalances.emplace(get_self(), [&](auto &_rambalance) {
            _rambalance.owner = account;
            _rambalance.byte_balance = bytes - 128;
        });
    } else {
        rambalances.modify(itr, same_payer, [&](auto &_rambalance) {
            _rambalance.byte_balance += bytes;
        });
    }
}


/**
* Internal function to decrease the ram balance of an account
* Throws if the account does not have enough balance
*/
void atomicpacks::decrease_ram_balance(
    name account,
    int64_t bytes,
    string error_message
) {
    check(bytes > 0, "decrease balance bytes must be positive");

    auto itr = rambalances.find(account.value);
    check(itr != rambalances.end() && itr->byte_balance >= bytes, error_message);

    rambalances.modify(itr, same_payer, [&](auto &_rambalance) {
        _rambalance.byte_balance -= bytes;
    });
}


/**
* Internal function to increase the ram balance of a collection
*/
void atomicpacks::increase_collection_ram_balance(
    name collection_name,
    int64_t bytes
) {
    check(bytes > 0, "increase balance bytes must be positive");

    auto itr = colbalances.find(collection_name.value);
    if (itr == colbalances.end()) {
        check(bytes >= 128, "Must inrease the collection ram balance by at least 128 to pay for the table entry");
        colbalances.emplace(get_self(), [&](auto &_colbalance) {
            _colbalance.collection_name = collection_name;
            _colbalance.byte_balance = bytes - 128;
        });
    } else {
        colbalances.modify(itr, same_payer, [&](auto &_colbalance) {
            _colbalance.byte_balance += bytes;
        });
    }
}


/**
* Internal function to decrease the ram balance of a collection
* Throws if the collection does not have enough balance
*/
void atomicpacks::decrease_collection_ram_balance(
    name collection_name,
    int64_t bytes,
    string error_message
) {
    check(bytes > 0, "decrease balance bytes must be positive");

    auto itr = colbalances.find(collection_name.value);
    check(itr != colbalances.end() && itr->byte_balance >= bytes, error_message);

    colbalances.modify(itr, same_payer, [&](auto &_colbalance) {
        _colbalance.byte_balance -= bytes;
    });
}
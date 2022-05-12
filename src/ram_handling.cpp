#include <atomicpacks.hpp>

/**
* Sells RAM bytes from the from the collection's ram balance and transfers the proceeds to recipient
* 
* @required_auth authorized_account, who needs to be an authorized account in the specified collection
*/
ACTION atomicpacks::withdrawram(
    name authorized_account,
    name collection_name,
    name recipient,
    int64_t bytes
) {
    require_auth(authorized_account);
    check_has_collection_auth(authorized_account, collection_name);

    check(is_account(recipient), "recipient account does not exist");

    decrease_collection_ram_balance(collection_name, bytes, "The collection does not have a sufficient ram balance");

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
            recipient,
            payout,
            string("Sold RAM")
        )
    ).send();
}


/**
* Refunds RAM bytes to a set of collections. This is meant to be used to refund bytes that get freed without being caught automatically,
* e.g. for when minted NFTs get burned
*
* @required_auth The contract itself
*/
ACTION atomicpacks::refundram(
    name refund_type,
    uint64_t from_block,
    uint64_t to_block,
    vector<RAM_REFUND_DATA> ram_refund_data
) {
    require_auth(get_self());

    check(to_block > from_block, "to_block needs to be larger than from_block");

    auto ramrefund_itr = ramrefunds.find(refund_type.value);

    if (ramrefund_itr == ramrefunds.end()) {
        check(from_block == 0, "For a new ramrefund type, the from_block needs to be 0");

        ramrefunds.emplace(get_self(), [&](auto &_ramrefund) {
            _ramrefund.refund_type = refund_type;
            _ramrefund.to_block = to_block;
        });

    } else {
        check(from_block == ramrefund_itr->to_block + 1, "For an existing ramrefund type, the from block needs to be the current to block + 1");

        ramrefunds.modify(ramrefund_itr, get_self(), [&](auto &_ramrefund) {
            _ramrefund.to_block = to_block;
        });
    }

    for (RAM_REFUND_DATA &ram_refund_element : ram_refund_data) {
        increase_collection_ram_balance(ram_refund_element.collection_name, ram_refund_element.bytes);
    }
}


/*
* Action that can only be called by the contract itself.
* 
* Having this in an extra action rather than directly in the on transfer notify is needed in order to
* prevent a reentrency attack that would otherwise open up due to the execution order of notifications
* and inline actions
*/
ACTION atomicpacks::buyramproxy(
    name collection_to_credit,
    asset quantity
) {
    require_auth(get_self());

    increase_collection_ram_balance(collection_to_credit, ram::get_purchase_ram_bytes(quantity));

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
* Internal function to increase the ram balance of a collection
*/
void atomicpacks::increase_collection_ram_balance(
    name collection_name,
    int64_t bytes
) {
    check(bytes > 0, "increase balance bytes must be positive");

    auto itr = rambalances.find(collection_name.value);
    if (itr == rambalances.end()) {
        check(bytes >= 128, "Must inrease the collection ram balance by at least 128 to pay for the table entry");
        rambalances.emplace(get_self(), [&](auto &_colbalance) {
            _colbalance.collection_name = collection_name;
            _colbalance.byte_balance = bytes - 128;
        });
    } else {
        rambalances.modify(itr, same_payer, [&](auto &_colbalance) {
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

    auto itr = rambalances.find(collection_name.value);
    check(itr != rambalances.end() && itr->byte_balance >= bytes, error_message);

    rambalances.modify(itr, same_payer, [&](auto &_colbalance) {
        _colbalance.byte_balance -= bytes;
    });
}
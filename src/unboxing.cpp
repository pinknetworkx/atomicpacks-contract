#include <atomicpacks.hpp>

#include <eosio/crypto.hpp>
#include <eosio/transaction.hpp>
#include <eosio/print.hpp>

#include "randomness_provider.cpp"


/**
* Claims one or more rolls from an unboxed pack.
* Claiming a roll can either mean that a new asset is minted if the template id is not -1
* or simply removing the row from the unboxassets table if the template id is -1
*
* @required_auth The unboxer of the pack
*/
ACTION atomicpacks::claimunboxed(
    uint64_t pack_asset_id,
    vector <uint64_t> origin_roll_ids
) {
    auto unboxpack_itr = unboxpacks.require_find(pack_asset_id,
        "No unboxpack with this pack asset id exists");

    require_auth(unboxpack_itr->unboxer);

    check(origin_roll_ids.size() != 0, "The original roll ids vector can't be empty");

    auto pack_itr = packs.find(unboxpack_itr->pack_id);


    atomicassets::templates_t col_templates = atomicassets::get_templates(pack_itr->collection_name);

    unboxassets_t unboxassets = get_unboxassets(pack_asset_id);

    int64_t ram_cost_delta = 0;
    bool mint_at_least_one = false;

    for (uint64_t roll_id : origin_roll_ids) {
        auto unboxasset_itr = unboxassets.require_find(roll_id,
            ("No unbox asset with the origin roll id " + to_string(roll_id) + " does not exist").c_str());

        if (unboxasset_itr->template_id != -1) {
            auto template_itr = col_templates.find(unboxasset_itr->template_id);
            //If the maximum supply for a template is already reached, the only option is to skip it
            //This can only happen if the supplies change between the time of receiving the randomness and claiming
            if (template_itr->max_supply == 0 || template_itr->issued_supply < template_itr->max_supply) {
                atomicassets::ATTRIBUTE_MAP immutable_data = {};
                atomicassets::ATTRIBUTE_MAP mutable_data = {};
                vector <asset> tokens_to_back = {};

                if (unboxasset_itr->mint_number_attribute_name != "") {
                    immutable_data[unboxasset_itr->mint_number_attribute_name] =
                        (uint32_t) template_itr->issued_supply + 1;
                    ram_cost_delta += 1 + get_varint_size(template_itr->issued_supply + 1);
                }
                if (unboxasset_itr->seed_attribute_name != "") {
                    immutable_data[unboxasset_itr->seed_attribute_name] = unboxasset_itr->seed;
                    ram_cost_delta += 9;
                }

                action(
                    permission_level{get_self(), name("active")},
                    atomicassets::ATOMICASSETS_ACCOUNT,
                    name("mintasset"),
                    make_tuple(
                        get_self(),
                        pack_itr->collection_name,
                        template_itr->schema_name,
                        template_itr->template_id,
                        unboxpack_itr->unboxer,
                        immutable_data,
                        mutable_data,
                        tokens_to_back
                    )
                ).send();
                mint_at_least_one = true;
                ram_cost_delta += 151;
            }
        }

        unboxassets.erase(unboxasset_itr);
        ram_cost_delta -=
            134 + unboxasset_itr->mint_number_attribute_name.length() + unboxasset_itr->seed_attribute_name.length();
    }

    if (mint_at_least_one) {
        atomicassets::assets_t unboxer_assets = atomicassets::get_assets(unboxpack_itr->unboxer);
        if (unboxer_assets.begin() == unboxer_assets.end()) {
            //Asset table scope
            ram_cost_delta += 112;
        }
    }
    if (unboxassets.begin() == unboxassets.end()) {
        unboxpacks.erase(unboxpack_itr);
        //Unboxassets table scope 112 + unboxpacks entry 144
        ram_cost_delta -= 256;

    }

    if (ram_cost_delta > 0) {
        check(decrease_collection_ram_balance(pack_itr->collection_name, ram_cost_delta),
            "The collection does not have enough RAM to mint the assets");
    } else if (ram_cost_delta < 0) {
        increase_collection_ram_balance(pack_itr->collection_name, -ram_cost_delta);
    }
}


/**
* This action is called by the rng oracle and provides the randomness for unboxing a pack
* The assoc id is equal to the asset id of the pack that is being unboxed
* 
* The unboxed assets are not immediately minted but instead placed in the unboxassets table with
* the scope <asset id of the pack that is being unboxed> and need to be claimed using the claimunboxed action
* This functionality is split in an effort to prevent transaction timeouts
* 
* @required_auth rng oracle account
*/
ACTION atomicpacks::receiverand(
    uint64_t assoc_id,
    checksum256 random_value
) {
    require_auth(orng::ORNG_CONTRACT);

    RandomnessProvider randomness_provider(random_value);

    auto unboxpack_itr = unboxpacks.find(assoc_id);

    auto pack_itr = packs.find(unboxpack_itr->pack_id);
    increase_collection_ram_balance(pack_itr->collection_name, unboxpack_itr->reserved_ram_bytes);


    atomicassets::templates_t col_templates = atomicassets::get_templates(pack_itr->collection_name);

    packrolls_t packrolls = get_packrolls(unboxpack_itr->pack_id);
    unboxassets_t unboxassets = get_unboxassets(unboxpack_itr->pack_asset_id);

    int64_t total_ram_cost = 0;

    for (auto roll_itr = packrolls.begin(); roll_itr != packrolls.end(); roll_itr++) {
        bool found_result = false;
        while (!found_result) {
            uint32_t rand = randomness_provider.get_rand(roll_itr->total_odds);
            uint32_t summed_odds = 0;

            for (const OUTCOME &outcome : roll_itr->outcomes) {
                summed_odds += outcome.odds;
                if (summed_odds >= rand) {
                    //If the template of this outcome has already reached its max supply,
                    //a new random value is used until an outcome is found which has a template
                    //that has not yet reached its max supply.
                    auto template_itr = col_templates.find(outcome.template_id);
                    if (template_itr->max_supply != 0 && template_itr->issued_supply == template_itr->max_supply) {
                        break;
                    }

                    //112 Scope, 8 roll_id, 4 template id, 2 x 1 string overhead, 8 seed
                    total_ram_cost +=
                        134 + outcome.mint_number_attribute_name.length() + outcome.seed_attribute_name.length();

                    unboxassets.emplace(get_self(), [&](auto &_unboxasset) {
                        _unboxasset.origin_roll_id = roll_itr->roll_id;
                        _unboxasset.template_id = outcome.template_id;
                        _unboxasset.mint_number_attribute_name = outcome.mint_number_attribute_name;
                        _unboxasset.seed_attribute_name = outcome.seed_attribute_name;
                        _unboxasset.seed = outcome.seed_attribute_name == "" ? 0 : randomness_provider.get_uint64();
                    });

                    found_result = true;
                    break;
                }
            }
        }
    }

    if (total_ram_cost >= 0) {
        //This should never happen because of the reserved RAM
        check(decrease_collection_ram_balance(pack_itr->collection_name, total_ram_cost + 112),
            "The collection does not have enough RAM to pay for the unboxassets entries.");
    }
}


/**
* This function is called when AtomicAssets assets are transferred to the pack contract

* This is used to unbox packs, by transferring exactly one pack to the pack contract
* The pack asset is then burned and the rng oracle is called to request a random value
*/
void atomicpacks::receive_asset_transfer(
    name from,
    name to,
    vector <uint64_t> asset_ids,
    string memo
) {
    if (to != get_self()) {
        return;
    }

    check(asset_ids.size() == 1, "Only one pack can be opened at a time");
    check(memo == "unbox", "Invalid memo");

    atomicassets::assets_t own_assets = atomicassets::get_assets(get_self());
    auto asset_itr = own_assets.find(asset_ids[0]);

    check(asset_itr->template_id != -1, "The transferred asset does not belong to a template");
    auto packs_by_template_id = packs.get_index<name("templateid")>();
    auto pack_itr = packs_by_template_id.require_find(asset_itr->template_id,
        "The transferred asset's template does not belong to any pack");


    action(
        permission_level{get_self(), name("active")},
        atomicassets::ATOMICASSETS_ACCOUNT,
        name("burnasset"),
        std::make_tuple(
            get_self(),
            asset_ids[0]
        )
    ).send();


    //Get signing value from transaction id
    //As this is only used as the signing value for the randomness oracle, it does not matter that this
    //signing value is not truly random
    size_t size = transaction_size();
    char buf[size];
    uint32_t read = read_transaction(buf, size);
    check(size == read, "read_transaction() has failed.");
    checksum256 tx_id = eosio::sha256(buf, read);
    uint64_t signing_value;
    memcpy(&signing_value, &tx_id, sizeof(signing_value));

    //Check if the signing_value was already used.
    //If that is the case, increment the signing_value until a non-used value is found
    while (orng::signvals.find(signing_value) != orng::signvals.end()) {
        signing_value++;
    }

    //112 for the unboxassets scope
    //262 bytes is the maximum size that each unboxassets table entry can have, because the attribute
    //names are limited to 64 chars max in AtomicAssets
    packrolls_t packrolls = get_packrolls(pack_itr->pack_id);
    int64_t reserved_ram_bytes = 112 + std::distance(packrolls.begin(), packrolls.end()) * 262;

    //144 for the unboxpacks entry (112 scope + 4 x 8)
    //120 for the signvals entry in the rng oracle contract
    check(decrease_collection_ram_balance(pack_itr->collection_name, reserved_ram_bytes + 144 + 120),
        "The collection does not have enough RAM to pay for the reserved bytes");

    unboxpacks.emplace(get_self(), [&](auto &_unboxpack) {
        _unboxpack.pack_asset_id = asset_ids[0];
        _unboxpack.pack_id = pack_itr->pack_id;
        _unboxpack.unboxer = from;
        _unboxpack.reserved_ram_bytes = reserved_ram_bytes;
    });

    action(
        permission_level{get_self(), name("active")},
        orng::ORNG_CONTRACT,
        name("requestrand"),
        std::make_tuple(
            asset_ids[0], //used as assoc id
            signing_value,
            get_self()
        )
    ).send();
}
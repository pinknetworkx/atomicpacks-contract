#include <atomicpacks.hpp>


/**
* Announces a new pack
* The possible outcomes packed in rolls must be provided afterwards with the addpackroll action
* 
* @required_auth authorized_account, who must be authorized within the specfied collection
*/
ACTION atomicpacks::announcepack(
    name authorized_account,
    name collection_name,
    uint32_t unlock_time,
    string display_data
) {
    require_auth(authorized_account);
    check_has_collection_auth(authorized_account, collection_name);

    check_has_collection_auth(get_self(), collection_name);

    uint64_t pack_id = packs.available_primary_key();
    if (pack_id == 0) {
        pack_id = 1;
    }
    
    packs.emplace(authorized_account, [&](auto &_pack) {
        _pack.pack_id = pack_id;
        _pack.collection_name = collection_name;
        _pack.unlock_time = unlock_time;
        _pack.pack_template_id = -1;
        _pack.roll_counter = 0;
        _pack.display_data = display_data;
    });


    action(
        permission_level{get_self(), name("active")},
        get_self(),
        name("lognewpack"),
        std::make_tuple(
            pack_id,
            collection_name,
            unlock_time
        )
    ).send();
}


/**
* Adds a roll to a pack
* A roll is a vector of outcomes, each of which have a probabilitiy (odds)
* The summed odds must equal 1
* 
* Each roll can be seen at one random chance at unboxing an NFT
* 
* @required_auth authorized_account, who must be authorized within the collection that the pack belongs to
*/
ACTION atomicpacks::addpackroll(
    name authorized_account,
    uint64_t pack_id,
    vector <OUTCOME> outcomes,
    uint32_t total_odds
) {
    require_auth(authorized_account);

    auto pack_itr = packs.require_find(pack_id, "No pack with this id exists");

    check_has_collection_auth(authorized_account, pack_itr->collection_name);

    check(pack_itr->pack_template_id == -1, "The pack has already been completed");


    check(outcomes.size() != 0, "A roll must include at least one outcome");


    atomicassets::templates_t col_templates = atomicassets::get_templates(pack_itr->collection_name);

    uint32_t total_counted_odds = 0;
    uint32_t last_odds = UINT_MAX;

    for (OUTCOME outcome : outcomes) {
        check(outcome.odds > 0, "Each outcome must have positive odds");
        check(outcome.odds <= last_odds,
            "The outcomes must be sorted in descending order based on their odds");
        last_odds = outcome.odds;

        total_counted_odds += outcome.odds;
        check(total_counted_odds >= outcome.odds, "Overflow: Total odds can't be more than 2^32 - 1");

        if (outcome.template_id != -1) {
            auto template_itr = col_templates.require_find(outcome.template_id,
                ("At least one template id of an outcome does not exist within the collection: " +
                 to_string(outcome.template_id)).c_str());
            check(template_itr->max_supply == 0, "Can only use templates without a max supply");
        }
    }

    check(total_counted_odds == total_odds,
        "The total odds of the outcomes deos not equal the provided total odds");


    uint64_t roll_id = pack_itr->roll_counter;

    packs.modify(pack_itr, same_payer, [&](auto &_pack) {
        _pack.roll_counter++;
    });

    packrolls_t packrolls = get_packrolls(pack_id);
    packrolls.emplace(authorized_account, [&](auto &_roll) {
        _roll.roll_id = roll_id;
        _roll.outcomes = outcomes;
        _roll.total_odds = total_odds;
    });


    action(
        permission_level{get_self(), name("active")},
        get_self(),
        name("lognewroll"),
        std::make_tuple(
            pack_id,
            roll_id
        )
    ).send();
}


/**
* Deletes a roll
* 
* @required_auth authorized_account, who must be authorized within the collection that the pack belongs to
*/
ACTION atomicpacks::delpackroll(
    name authorized_account,
    uint64_t pack_id,
    uint64_t roll_id
) {
    require_auth(authorized_account);

    auto pack_itr = packs.require_find(pack_id, "No pack with this id exists");

    check_has_collection_auth(authorized_account, pack_itr->collection_name);

    check(pack_itr->pack_template_id == -1, "The pack has already been completed");


    packrolls_t packrolls = get_packrolls(pack_id);
    auto roll_itr = packrolls.require_find(roll_id, "No roll with this id exists for the specified pack");

    packrolls.erase(roll_itr);
}


/**
* Completes a pack
* By completing a pack, it is linked to the specified template id, which means that every asset belonging
* to this template is then viewed as a pack that can be unboxed
* 
* After a pack is completed, no new rolls can be added and no existing rolls can be erased
* 
* @required_auth authorized_account, who must be authorized within the collection that the pack belongs to
*/
ACTION atomicpacks::completepack(
    name authorized_account,
    uint64_t pack_id,
    int32_t pack_template_id
) {
    require_auth(authorized_account);

    auto pack_itr = packs.require_find(pack_id, "No pack with this id exists");

    check_has_collection_auth(authorized_account, pack_itr->collection_name);


    check(pack_itr->pack_template_id == -1, "The pack has already been completed");

    packrolls_t packrolls = get_packrolls(pack_id);
    check(packrolls.begin() != packrolls.end(), "The pack does not have any rolls");


    check(pack_template_id > 0, "The tempalte id must be positive");
    atomicassets::templates_t col_templates = atomicassets::get_templates(pack_itr->collection_name);
    auto template_itr = col_templates.require_find(pack_template_id,
        "No template with this id exists within the collection taht the pack belongs to");
    check(template_itr->burnable, "The template with this id is not burnable.");


    auto packs_by_template_id = packs.get_index<name("templateid")>();
    check(packs_by_template_id.find(pack_template_id) == packs_by_template_id.end(),
        "Another pack is already using this template id");

    packs.modify(pack_itr, same_payer, [&](auto &_pack) {
        _pack.pack_template_id = pack_template_id;
    });
}


ACTION atomicpacks::setpacktime(
    name authorized_account,
    uint64_t pack_id,
    uint32_t new_unlock_time
) {
    require_auth(authorized_account);

    auto pack_itr = packs.require_find(pack_id, "No pack with this id exists");

    check_has_collection_auth(authorized_account, pack_itr->collection_name);

    check(pack_itr->pack_template_id != -1, "The pack has not been completed yet");

    check(new_unlock_time > current_time_point().sec_since_epoch(),
        "The new unlock time can't be in the past");

    packs.modify(pack_itr, same_payer, [&](auto &_pack) {
        _pack.unlock_time = new_unlock_time;
    });
}


ACTION atomicpacks::setpackdata(
    name authorized_account,
    uint64_t pack_id,
    string display_data
) {
    require_auth(authorized_account);

    auto pack_itr = packs.require_find(pack_id, "No pack with this id exists");

    check(pack_itr->pack_template_id != -1, "The pack has not been completed yet");

    packs.modify(pack_itr, authorized_account, [&](auto &_pack) {
        _pack.display_data = display_data;
    });
}


ACTION atomicpacks::lognewpack(
    uint64_t pack_id,
    name collection_name,
    uint32_t unlock_time
) {
    require_auth(get_self());
}


ACTION atomicpacks::lognewroll(
    uint64_t pack_id,
    uint64_t roll_id
) {
    require_auth(get_self());
}
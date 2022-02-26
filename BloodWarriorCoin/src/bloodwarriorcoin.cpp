#include <bloodwarriorcoin.hpp>

bool bloodwarriorcoin::is_valid_transaction(const name &from,
                                        const name &to)
{
   bloodwarriors_table _bloodwarriors(bloodwarriorcode_account, bloodwarriorcode_account.value);
   auto bloodwarrior_itr_from = _bloodwarriors.find(from.value);
   auto bloodwarrior_itr_to = _bloodwarriors.find(to.value);
   // bloodwarrior --> bloodwarrior
   if (bloodwarrior_itr_from != _bloodwarriors.end() && bloodwarrior_itr_to != _bloodwarriors.end())
   {
      return true;
   }
   donors_table _donors(bloodwarriorcode_account, bloodwarriorcode_account.value);
   auto donor_itr_to = _donors.find(to.value);
   // bloodwarrior --> doner
   if (bloodwarrior_itr_from != _bloodwarriors.end() && donor_itr_to != _donors.end())
   {
      return true;
   }
   auto donor_itr_from = _donors.find(from.value);
   sponsors_table _sponsors(bloodwarriorcode_account, bloodwarriorcode_account.value);
   auto sponsor_itr_to = _sponsors.find(to.value);
   // donor --> sponsor
   if (donor_itr_from != _donors.end() && sponsor_itr_to != _sponsors.end())
   {
      return true;
   }
   auto sponsor_itr_from = _sponsors.find(from.value);
   bloodwarrior_itr_to = _bloodwarriors.find(to.value);
   // sponsor --> bloodwarrior
   if (sponsor_itr_from != _sponsors.end() && bloodwarrior_itr_to != _bloodwarriors.end())
   {
      return true;
   }
   return false;
}

void bloodwarriorcoin::sub_balance(const name &owner, const asset &value)
{
   accounts from_acnts(get_self(), owner.value);

   const auto &from = from_acnts.get(value.symbol.code().raw(), "no balance object found");
   check(from.balance.amount >= value.amount, "overdrawn balance");

   from_acnts.modify(from, get_self(), [&](auto &a) {
      a.balance -= value;
   });
}

// add_balance(bloodwarrior, quantity, bloodwarrior);
void bloodwarriorcoin::add_balance(const name &owner, const asset &value, const name &ram_payer)
{
   accounts to_acnts(get_self(), owner.value);
   auto to = to_acnts.find(value.symbol.code().raw());
   if (to == to_acnts.end())
   {
      to_acnts.emplace(get_self(), [&](auto &row) {
         row.balance = value;
      });
   }
   else
   {
      to_acnts.modify(to, get_self(), [&](auto &row) {
         row.balance += value;
      });
   }
}

ACTION bloodwarriorcoin::create(const name &issuer,
                            const asset &maximum_supply)
{
   require_auth(name("bloodwarriorcode"));
   auto sym = maximum_supply.symbol;
   check(sym.is_valid(), "invalid symbol name");
   check(maximum_supply.is_valid(), "invalid supply");
   check(maximum_supply.amount > 0, "max-supply must be positive");

   stats statstable(get_self(), sym.code().raw());
   auto existing = statstable.find(sym.code().raw());
   check(existing == statstable.end(), "token with symbol already exists");

   statstable.emplace(get_self(), [&](auto &s) {
      s.supply.symbol = maximum_supply.symbol;
      s.max_supply = maximum_supply;
      s.issuer = issuer;
   });
}

ACTION bloodwarriorcoin::issue(const name &bloodwarrior, const name &donor, const string &memo)
{
   require_auth(bloodwarrior);

   bloodwarriors_table _bloodwarriors(bloodwarriorcode_account, bloodwarriorcode_account.value);
   auto bloodwarrior_itr = _bloodwarriors.find(bloodwarrior.value);
   check(bloodwarrior_itr != _bloodwarriors.end(), "please a provide a valid bloodwarrior name");

   const auto &bloodwarrior_data = *bloodwarrior_itr;
   const auto blood_urgency_level = bloodwarrior_data.blood_urgency_level;
   const auto bloodwarrior_symbol = bloodwarrior_data.community;

   communities_table community(bloodwarriorcode_account, bloodwarriorcode_account.value);
   auto existing_cmm = community.find(bloodwarrior_symbol.raw());
   check(existing_cmm != community.end(), "community does not exists");

   check(memo.size() <= 256, "memo has more than 256 bytes");

   stats statstable(get_self(), bloodwarrior_symbol.code().raw());
   auto existing = statstable.find(bloodwarrior_symbol.code().raw());
   check(existing != statstable.end(), "token with symbol does not exist, create token before issuing");

   auto id = gen_uuid(bloodwarrior_symbol.raw(), donor.value);
   networks_table network(bloodwarriorcode_account, bloodwarriorcode_account.value);
   auto existing_netlink = network.find(id);
   check(existing_netlink != network.end(), "donor must to belong to community");

   const auto &st = *existing;
   // check(to == st.issuer, "tokens can only be issued to issuer account");
   auto quantity = eosio::asset(blood_urgency_level, bloodwarrior_symbol);

   check(quantity.is_valid(), "invalid quantity");
   check(quantity.amount > 0, "must issue positive quantity");

   check(quantity.symbol == st.supply.symbol, "symbol precision mismatch");
   check(quantity.amount <= st.max_supply.amount - st.supply.amount, "quantity exceeds available supply");

   statstable.modify(st, get_self(), [&](auto &s) {
      s.supply += quantity;
   });

   add_balance(bloodwarrior, quantity, bloodwarrior);
   action(
       permission_level{get_self(), "active"_n},
       get_self(),
       "transferlife"_n,
       std::make_tuple(bloodwarrior, donor, quantity, memo))
       .send();
}

ACTION bloodwarriorcoin::transfer(const name &from,
                              const name &to,
                              const asset &quantity,
                              const string &memo)
{
   check(from != to, "cannot transfer to self");
   require_auth(from);

   check(is_valid_transaction(from, to), "invalid transaction");

   check(is_account(to), "to account does not exist");
   auto sym = quantity.symbol.code();
   stats statstable(get_self(), sym.raw());
   const auto &st = statstable.get(sym.raw());

   require_recipient(from);
   require_recipient(to);

   check(quantity.is_valid(), "invalid quantity");
   check(quantity.amount > 0, "must transfer positive quantity");
   check(quantity.symbol == st.supply.symbol, "symbol precision mismatch");
   check(memo.size() <= 256, "memo has more than 256 bytes");

   auto payer = has_auth(to) ? to : from;

   sub_balance(from, quantity);
   add_balance(to, quantity, payer);
}

ACTION bloodwarriorcoin::transferlife(const name &from,
                                  const name &to,
                                  const asset &quantity,
                                  const string &memo)
{
   check(from != to, "cannot transfer to self");
   require_auth(get_self());

   check(is_valid_transaction(from, to), "invalid transaction");

   check(is_account(to), "to account does not exist");
   auto sym = quantity.symbol.code();
   stats statstable(get_self(), sym.raw());
   const auto &st = statstable.get(sym.raw());

   require_recipient(from);
   require_recipient(to);

   check(quantity.is_valid(), "invalid quantity");
   check(quantity.amount > 0, "must transfer positive quantity");
   check(quantity.symbol == st.supply.symbol, "symbol precision mismatch");
   check(memo.size() <= 256, "memo has more than 256 bytes");

   auto payer = has_auth(to) ? to : from;

   sub_balance(from, quantity);
   add_balance(to, quantity, payer);
}

ACTION bloodwarriorcoin::redeemoffer(uint64_t offer_comm_id, name donor_name)
{
   require_auth(get_self());
   
   const name payer = name("bloodwarriorcode");
   bloodwarriorcode::bloodwarrior_offers_table _bloodwarrior_offers(payer, payer.value);

   // START - Check if offer exist
   auto linkoffers_itr = _bloodwarrior_offers.find(offer_comm_id);
   check(linkoffers_itr != _bloodwarrior_offers.end(), "offer not exist");
   // END - Check if offer exist

   // START - Check if donor has funds to redeem the selected offer
   accounts from_acnts(get_self(), donor_name.value);
   bloodwarriorcode::offers_table _offers(payer, payer.value);

   const auto &offercomm_row = _bloodwarrior_offers.get(offer_comm_id);
   const auto &offer_row = _offers.get(offercomm_row.offer_name.value);
   const auto &from = from_acnts.get(offer_row.cost.symbol.code().raw(), "no balance object found");

   check(from.balance.amount >= offer_row.cost.amount, "overdrawn balance");
   // END - Check if offer exist

   redeem_offer_table _redeem_offer(get_self(), get_self().value);

   _redeem_offer.emplace(get_self(), [&](auto &row) {
      row.id = _redeem_offer.available_primary_key();
      row.donor_name = donor_name;
      row.offer_comm_id = offer_comm_id;
   });
}

ACTION bloodwarriorcoin::clear(const asset &current_asset, const name owner)
{
   require_auth(get_self());
   auto sym = current_asset.symbol;
   check(sym.is_valid(), "invalid symbol name");
   stats statstable(get_self(), sym.code().raw());
   auto existing = statstable.find(sym.code().raw());
   // check(existing != statstable.end(), "token with symbol does not exist");
   auto stats_itr = statstable.begin();
   while (stats_itr != statstable.end())
   {
      stats_itr = statstable.erase(stats_itr);
   }
   accounts acnts(get_self(), owner.value);
   auto sym_acnts = acnts.find(sym.code().raw());
   // check(sym_acnts != acnts.end(), "token with symbol does not exist");
   auto acnts_itr = acnts.begin();
   while (acnts_itr != acnts.end())
   {
      acnts_itr = acnts.erase(acnts_itr);
   }
}
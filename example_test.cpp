#include <iostream>
#include <algorithm>

#include "hoytech-cpp/hoytech/assert_zerocopy.h"
#include "build/example.h"


#define verify(condition) do { if (!(condition)) throw hoytech::error(#condition, "  |  ", __FILE__, ":", __LINE__); } while(0)
#define verifyThrow(condition, expected) { \
    bool caught = false; \
    std::string errorMsg; \
    try { condition; } \
    catch (const std::runtime_error &e) { \
        caught = true; \
        errorMsg = e.what(); \
    } \
    if (!caught) throw hoytech::error(#condition, " | expected error, but didn't get one (", expected, ")"); \
    if (errorMsg.find(expected) == std::string::npos) throw hoytech::error(#condition, " | error msg not what we expected: ", errorMsg, " (not ", expected, ")"); \
}



int main() {
    example::environment env;

    verify(system("mkdir -p db/") == 0);
    verify(system("rm -f db/data.mdb") == 0);

    env.open("db/");

    // Populate some records

    {
        auto txn = env.txn_rw();

        env.insert_User(txn, "john", "\x01\x02\x03", 1000); // 1
        env.insert_User(txn, "jane", "\x01\x02\x03", 1001); // 2
        env.insert_User(txn, "jane2", "\x01\x02\x03", 1001); // 3
        env.insert_User(txn, "alice", "\x01\x02\x03", 2000); // 4
        env.insert_User(txn, "bob", "\x01\x02\x03", 1500); // 5
        env.insert_User(txn, "bob2", "\xFF", 1499); // 6
        env.insert_User(txn, "", "", 0); // 7

        txn.commit();
    }

    // Unique constraint

    {
        auto txn = env.txn_rw();
        verifyThrow(env.insert_User(txn, "jane", "", 3000), "unique constraint violated: User.userName");
    }

    // Lookup single record by primary key

    {
        auto txn = env.txn_ro();
        auto view = env.lookup_User(txn, 2);

        verify(view);
        verify(view->primaryKeyId == 2);
        verify(view->userName() == "jane");
        verify(view->passwordHash() == "\x01\x02\x03");
        verify(view->created() == 1001);

        assert_zerocopy(env.lmdb_env.get_internal_map(), view->userName());
    }

    // Lookup single record by index

    {
        auto txn = env.txn_ro();
        auto view = env.lookup_User__userName(txn, "alice");

        verify(view);
        verify(view->primaryKeyId == 4);
        verify(view->userName() == "alice");
        verify(view->passwordHash() == "\x01\x02\x03");
        verify(view->created() == 2000);
    }

    // Lookup single record by index, when there are multiple matches just takes first it finds

    {
        auto txn = env.txn_ro();
        auto view = env.lookup_User__created(txn, 1001);

        verify(view);
        verify(view->created() == 1001);
    }

    // Update record, no index updates

    {
        auto txn = env.txn_rw();
        auto view = env.lookup_User__userName(txn, "alice");
        auto ret = env.update_User(txn, *view, { .passwordHash = "\xDD\xEE" });
        verify(ret != 0);
        txn.commit();
    }

    {
        auto txn = env.txn_rw();
        auto view = env.lookup_User__userName(txn, "alice");

        verify(view);
        verify(view->primaryKeyId == 4);
        verify(view->userName() == "alice");
        verify(view->passwordHash() == "\xDD\xEE");
        verify(view->created() == 2000);
    }

    // Update record, no changes

    {
        auto txn = env.txn_rw();
        auto view = env.lookup_User__userName(txn, "alice");

        auto ret = env.update_User(txn, *view, { .created = 2000 });
        verify(ret == 0);

        txn.commit();
    }

    // Iterate over table

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreach_User(txn, [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        });

        verify(ids == std::vector<uint64_t>({1, 2, 3, 4, 5, 6, 7}));
    }

    // Iterate over table in reverse

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreach_User(txn, [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        }, true);

        verify(ids == std::vector<uint64_t>({7, 6, 5, 4, 3, 2, 1}));
    }

    // Iterate over table with starting point

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreach_User(txn, [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        }, false, 3);

        verify(ids == std::vector<uint64_t>({3, 4, 5, 6, 7}));
    }

    // Iterate over string index

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;
        uint64_t total;

        env.foreach_User__userName(txn, [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        }, false, std::nullopt, &total);

        verify(ids == std::vector<uint64_t>({4, 5, 6, 2, 3, 1}));
        verify(total == 6);
    }

    // Iterate over string index, start at "bob"

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;
        uint64_t total;

        env.foreach_User__userName(txn, [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        }, false, "bob", &total);

        verify(ids == std::vector<uint64_t>({5, 6, 2, 3, 1}));
        verify(total == 6); // full index count
    }

    // Iterate over string index, start at "amy", which doesn't exist. It should start with bob

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreach_User__userName(txn, [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        }, false, "amy");

        verify(ids == std::vector<uint64_t>({5, 6, 2, 3, 1}));
    }

    // Iterate over string index in reverse, start at "carol", which doesn't exist. It should start with bob2

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreach_User__userName(txn, [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        }, true, "carol");

        verify(ids == std::vector<uint64_t>({6, 5, 4}));
    }

    // Iterate over numeric index

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreach_User__created(txn, [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        });

        verify(ids == std::vector<uint64_t>({1, 2, 3, 6, 5, 4}));
    }

    // Iterate over numeric index in reverse, start at "5" and stop after "3"

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreach_User__created(txn, [&](auto &view){
            ids.push_back(view.primaryKeyId);
            if (view.primaryKeyId == 3) return false;
            return true;
        }, true, 1500);

        verify(ids == std::vector<uint64_t>({5, 6, 3}));
    }




    // Update record, update indices

    {
        auto txn = env.txn_rw();
        auto view = env.lookup_User__userName(txn, "alice");
        env.update_User(txn, *view, { .userName = "zoya", .created = 1001, });
        txn.commit();
    }

    {
        auto txn = env.txn_rw();
        auto view = env.lookup_User__userName(txn, "zoya");

        verify(view);
        verify(view->primaryKeyId == 4);
        verify(view->userName() == "zoya");
    }

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreach_User__userName(txn, [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        });

        verify(ids == std::vector<uint64_t>({5, 6, 2, 3, 1, 4}));
    }

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreachDup_User__created(txn, 1001, [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        });

        verify(ids == std::vector<uint64_t>({2, 3, 4}));
    }




    // Iterate over dup records in created index

    {
        auto txn = env.txn_rw();
        auto view = env.lookup_User(txn, 6);
        env.update_User(txn, *view, { .created = 1001, });
        txn.commit();
    }

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;
        uint64_t total;

        env.foreachDup_User__created(txn, 1001, [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        }, false, std::nullopt, &total);

        verify(ids == std::vector<uint64_t>({2, 3, 4, 6}));
        verify(total == 4);
    }

    // Iterate over dups in reverse

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreachDup_User__created(txn, 1001, [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        }, true);

        verify(ids == std::vector<uint64_t>({6, 4, 3, 2}));
    }

    // Iterate over dups with starting point

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreachDup_User__created(txn, 1001, [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        }, false, 5);

        verify(ids == std::vector<uint64_t>({6}));
    }

    // Iterate over dups with pre-starting point

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreachDup_User__created(txn, 1001, [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        }, false, 1);

        verify(ids == std::vector<uint64_t>({2, 3, 4, 6}));
    }

    // Iterate over dups in reverse with starting point

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreachDup_User__created(txn, 1001, [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        }, true, 5);

        verify(ids == std::vector<uint64_t>({4, 3, 2}));
    }

    // Iterate over dups in reverse with starting point, skip

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreachDup_User__created(txn, 1001, [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        }, true, 500);

        verify(ids == std::vector<uint64_t>({6, 4, 3, 2}));
    }

    // Iterate over dups in reverse with starting point, no records

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreachDup_User__created(txn, 1001, [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        }, true, 1);

        verify(ids == std::vector<uint64_t>({}));
    }



    // Delete

    {
        auto txn = env.txn_rw();
        env.delete_User(txn, 3);
        env.delete_User(txn, 7);
        txn.commit();
    }

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreach_User(txn, [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        });

        verify(ids == std::vector<uint64_t>({1, 2, 4, 5, 6}));
    }

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreach_User__userName(txn, [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        });

        verify(ids == std::vector<uint64_t>({5, 6, 2, 1, 4}));
    }

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreachDup_User__created(txn, 1001, [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        });

        verify(ids == std::vector<uint64_t>({2, 4, 6}));
    }

    // Iterate over dups in reverse with starting point, no dups at all

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreachDup_User__created(txn, 1002, [&](auto &view){
            std::cout << view.primaryKeyId << ": " << view._str() << std::endl;
            ids.push_back(view.primaryKeyId);
            return true;
        }, true, 1);

        verify(ids == std::vector<uint64_t>({}));
    }






    // Computed indices

    {
        auto txn = env.txn_rw();

        env.insert_Person(txn, "John", "john@GMAIL.COM", 20, "user");
        env.insert_Person(txn, "john", "John@Yahoo.Com", 30, "user");
        env.insert_Person(txn, "alice", "alice@gmail.com", 5, "user");
        env.insert_Person(txn, "sam", "sam@gmail.com", 40, "admin");

        txn.commit();
    }

    {
        auto txn = env.txn_ro();
        auto view = env.lookup_Person__emailLC(txn, "john@gmail.com");

        verify(view);
        verify(view->primaryKeyId == 1);
        verify(view->email() == "john@GMAIL.COM");
    }

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreachDup_Person__fullNameLC(txn, "john", [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        });

        verify(ids == std::vector<uint64_t>({1, 2}));
    }

    {
        auto txn = env.txn_rw();
        verifyThrow(env.insert_Person(txn, "john", "john@Yahoo.Com", 30, "user"), "unique constraint violated: Person.emailLC");
    }

    // Alice is not indexed because age < 18

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreach_Person__age(txn, [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        });

        verify(ids == std::vector<uint64_t>({1, 2, 4}));
    }

    // Sam is not indexed because role is admin

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreach_Person__role(txn, [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        });

        verify(ids == std::vector<uint64_t>({1, 2, 3}));
    }



    // Multi indices

    {
        auto txn = env.txn_rw();

        env.insert_Phrase(txn, "the quick brown"); // 1
        env.insert_Phrase(txn, "fox jumped over"); // 2
        env.insert_Phrase(txn, "a quick but lazy"); // 3
        env.insert_Phrase(txn, "dog"); // 4
        env.insert_Phrase(txn, "one more quick"); // 5

        txn.commit();
    }

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreachDup_Phrase__splitWords(txn, "quick", [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        });

        verify(ids == std::vector<uint64_t>({1, 3, 5}));
    }

    {
        auto txn = env.txn_ro();

        auto view = env.lookup_Phrase(txn, 2);

        auto indices = env.getIndices_Phrase(*view);

        verify(indices.splitWords == std::vector<std::string>({ "fox", "jumped", "over" }));
    }

    {
        auto txn = env.txn_rw();
        env.delete_Phrase(txn, 3);
        txn.commit();
    }

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreachDup_Phrase__splitWords(txn, "quick", [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        });

        verify(ids == std::vector<uint64_t>({1, 5}));
    }






    // Custom primary key

    {
        auto txn = env.txn_rw();

        env.insert_SomeRecord(txn, 53, "b");
        env.insert_SomeRecord(txn, 99, "f");
        env.insert_SomeRecord(txn, 70, "d");
        env.insert_SomeRecord(txn, 60, "c");
        env.insert_SomeRecord(txn, 75, "e");
        env.insert_SomeRecord(txn, 50, "a");

        txn.commit();
    }

    // Iterate over table

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreach_SomeRecord(txn, [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        });

        verify(ids == std::vector<uint64_t>({50, 53, 60, 70, 75, 99}));
    }

    // Iterate over table with starting point

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreach_SomeRecord(txn, [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        }, false, 60);

        verify(ids == std::vector<uint64_t>({60, 70, 75, 99}));
    }

    // Iterate over table with starting point, skip

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreach_SomeRecord(txn, [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        }, false, 61);

        verify(ids == std::vector<uint64_t>({70, 75, 99}));
    }

    // Iterate over table in reverse with starting point

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreach_SomeRecord(txn, [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        }, true, 60);

        verify(ids == std::vector<uint64_t>({60, 53, 50}));
    }

    // Iterate over table in reverse with starting point, skip

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreach_SomeRecord(txn, [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        }, true, 61);

        verify(ids == std::vector<uint64_t>({60, 53, 50}));
    }

    // Iterate over table in reverse with starting point, skip

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreach_SomeRecord(txn, [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        }, true, 100);

        verify(ids == std::vector<uint64_t>({99, 75, 70, 60, 53, 50}));
    }




    // Multi records

    {
        auto txn = env.txn_rw();

        env.insert_MultiRecs(txn, { "hello", "world" }, { "\xFF\xEE", "\xF5\xF5" }, { 3, 4 }); // 1

        {
            std::vector<std::string> strs = { "goodbye", "world" };
            env.insert_MultiRecs(txn, env.views(strs), { "\xF5\xF5" }, { 4, 5, 6 }); // 2
        }

        txn.commit();
    }

    {
        auto txn = env.txn_ro();

        auto view = env.lookup_MultiRecs(txn, 1);

        verify(view);
        verify(view->strs().size() == 2);
        verify(view->strs()[0] == "hello");
        verify(view->strs()[1] == "world");

        assert_zerocopy(env.lmdb_env.get_internal_map(), view->strs()[0]);
    }

    // Iterate over string index

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;
        uint64_t total;

        env.foreach_MultiRecs__strs(txn, [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        }, false, std::nullopt, &total);

        verify(ids == std::vector<uint64_t>({2, 1, 1, 2}));
        verify(total == 4);
    }

    // Iterate over dups

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;
        uint64_t total;

        env.foreachDup_MultiRecs__strs(txn, "world", [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        }, false, std::nullopt, &total);

        verify(ids == std::vector<uint64_t>({1, 2}));
        verify(total == 2);
    }

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreachDup_MultiRecs__strs(txn, "goodbye", [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        });

        verify(ids == std::vector<uint64_t>({2}));
    }

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreachDup_MultiRecs__ubytesField(txn, "\xF5\xF5", [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        });

        verify(ids == std::vector<uint64_t>({1, 2}));
    }

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreachDup_MultiRecs__ints(txn, 4, [&](auto &view){
            //std::cout << view.primaryKeyId << ": " << view._str() << std::endl;
            ids.push_back(view.primaryKeyId);
            return true;
        });

        verify(ids == std::vector<uint64_t>({1, 2}));
    }
 
    {
        auto txn = env.txn_rw();
        env.delete_MultiRecs(txn, 1);
        txn.commit();
    }

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreachDup_MultiRecs__strs(txn, "world", [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        });

        verify(ids == std::vector<uint64_t>({2}));
    }

    std::cout << "All tests OK." << std::endl;

    return 0;
}

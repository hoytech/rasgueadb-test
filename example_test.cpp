#include <iostream>
#include <algorithm>

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
        auto view = env.lookup_User__created(txn, lmdb::to_sv<uint64_t>(1001));

        verify(view);
        verify(view->created() == 1001);
    }

    // Update record, no index updates

    {
        auto txn = env.txn_rw();
        auto view = env.lookup_User__userName(txn, "alice");
        env.update_User(txn, *view, { .passwordHash = "\xDD\xEE" });
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

    // Iterate over table

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreach_User(txn, [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        });

        verify(ids == std::vector<uint64_t>({1, 2, 3, 4, 5, 6}));
    }

    // Iterate over string index

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreach_User__userName(txn, [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        }, std::nullopt);

        verify(ids == std::vector<uint64_t>({4, 5, 6, 2, 3, 1}));
    }

    // Iterate over string index, start at "bob"

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreach_User__userName(txn, [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        }, "bob");

        verify(ids == std::vector<uint64_t>({5, 6, 2, 3, 1}));
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
        }, lmdb::to_sv<uint64_t>(1500), true);

        verify(ids == std::vector<uint64_t>({5, 6, 3}));
    }

    // Iterate over dup records in created index

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreachDup_User__created(txn, lmdb::to_sv<uint64_t>(1001), [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        });

        verify(ids == std::vector<uint64_t>({2, 3}));
    }

    // Iterate over dups in reverse

    {
        auto txn = env.txn_ro();

        std::vector<uint64_t> ids;

        env.foreachDup_User__created(txn, lmdb::to_sv<uint64_t>(1001), [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        }, true);

        verify(ids == std::vector<uint64_t>({3, 2}));
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

        env.foreachDup_User__created(txn, lmdb::to_sv<uint64_t>(1001), [&](auto &view){
            //std::cout << view.primaryKeyId << ": " << view._str() << std::endl;
            ids.push_back(view.primaryKeyId);
            return true;
        });

        verify(ids == std::vector<uint64_t>({2, 3, 4}));
    }



    // Delete

    {
        auto txn = env.txn_rw();
        env.delete_User(txn, 3);
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

        env.foreachDup_User__created(txn, lmdb::to_sv<uint64_t>(1001), [&](auto &view){
            ids.push_back(view.primaryKeyId);
            return true;
        });

        verify(ids == std::vector<uint64_t>({2, 4}));
    }






    // Computed indices

    {
        auto txn = env.txn_rw();

        env.insert_Person(txn, "John", "john@GMAIL.COM");
        env.insert_Person(txn, "john", "John@Yahoo.Com");

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
        verifyThrow(env.insert_Person(txn, "john", "john@Yahoo.Com"), "unique constraint violated: Person.emailLC");
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


    std::cout << "All tests OK." << std::endl;

    return 0;
}

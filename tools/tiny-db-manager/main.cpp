#include <iostream>
#include <string>
#include <vector>
#include <format>
#include <SQLiteCpp/SQLiteCpp.h>

// .\miniprj-dbmanager.exe
// [DB_PATH] [insert/delete/select] [TABLE_NAME] [KEYWORD] 
int main(int argc, char* argv[]) {
	if (argc != 4 && argc != 5) {
		std::cerr << argv[0] << "[DB_PATH] [insert/delete/select] [TABLE_NAME] [KEYWORD]" << std::endl;
		return 1;
	}

	try {
		SQLite::Database db(argv[1], SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);

		std::string insertStr("insert");
		std::string deleteStr("delete");
		std::string selectStr("select");
		std::string includeCol("userdefined_include");
		std::string excludeCol("userdefined_exclude");

		if (insertStr.compare(argv[2]) == 0) {
			std::cout << "@insert" << std::endl;
			std::string insertCmd = std::format("INSERT INTO {} VALUES (\"{}\")", argv[3], argv[4]);
			int nb = db.exec(insertCmd);
			std::cout << insertCmd << ", returned " << nb << std::endl;
			return 0;
		}
		else if (deleteStr.compare(argv[2]) == 0) {
			std::cout << "@delete" << std::endl;
			if (includeCol.compare(argv[3]) == 0) {
				std::string deleteCmd = std::format("DELETE FROM {} WHERE {} LIKE (\"{}\")", argv[3], "include", argv[4]);
				int nb = db.exec(deleteCmd);
				std::cout << deleteCmd << ", returned " << nb << std::endl;
			}
			else if (excludeCol.compare(argv[3]) == 0) {
				std::string deleteCmd = std::format("DELETE FROM {} WHERE {} LIKE (\"{}\")", argv[3], "exclude", argv[4]);
				int nb = db.exec(deleteCmd);
				std::cout << deleteCmd << ", returned " << nb << std::endl;
			}
			else {
				return 1;
			}
			return 0;
		}
		else if (selectStr.compare(argv[2]) == 0) {
			std::vector<std::string> strList;
			std::cout << "@select" << std::endl;
			std::string selectCmd = std::format("SELECT * FROM  (\"{}\")", argv[3]);
			SQLite::Statement cmd(db, selectCmd);
			while (cmd.executeStep())
			{
				strList.push_back(cmd.getColumn(0));
			}

			for (int i = 0; i < strList.size(); i++) {
				std::cout << strList.at(i) << std::endl;
			}
			return 0;
		}
		else {
			return 1;
		}
	}
	catch (std::exception e) {
		std::cerr << e.what() << std::endl;
	}
	return 1;
}
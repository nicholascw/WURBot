#include "config_release.h"

using namespace std;
using namespace TgBot;
bool sigintGot = false;
time_t birthTime(time(nullptr));
unordered_map<int32_t, time_t> *jail_ptr;

typedef enum {
    NEW,
    OPT1,
    OPT2,
} poll_stage;

typedef struct _pending_poll_stage {
    uint32_t hash = 0;
    int32_t user_id;
    int32_t admin_channel_msgid = 0;
    int64_t user_chatid = 0;
    poll_stage stage;
    std::string username;
    std::string assumption;
    std::string opt1;
    std::string opt2;
} pending_stage;

std::string json_escape(std::string instr) {
    boost::ireplace_all(instr, "\\", "\\\\");
    boost::ireplace_all(instr, "\"", "\\\"");
    return instr;
}

bool is_in_jail(int32_t userid) {
    if(!jail_ptr) return false;
    unordered_map<int32_t, time_t>::iterator tmp_obj;
    if((tmp_obj = jail_ptr->find(userid)) != jail_ptr->end()) {
        if(tmp_obj->second > time(nullptr)) {
            return true;
        } else {
            jail_ptr->erase(tmp_obj);
            return false;
        }
    } else return false;
}

bool is_admin(Bot &bot, int32_t user_id) {
    for(const auto &i : bot.getApi().getChatAdministrators(channel_id))
        if(i->user->id == user_id && (i->canPostMessages || i->status == "creator")) return true;
    return false;
}

#pragma clang diagnostic push
#pragma ide diagnostic ignored "hicpp-signed-bitwise"

uint32_t get_hash() {
    uint64_t x = (uint64_t) clock() ^(uint64_t) time(nullptr);
    x = ((x & 0xffffffff00000000) >> 32) ^ x;
    uint32_t h = x & 0x00000000ffffffff;
    return h;
}

#pragma clang diagnostic pop

int32_t send_to_admin_channel(Bot &bot, pending_stage &new_post) {
    InlineKeyboardMarkup::Ptr decision_kbd(new InlineKeyboardMarkup);
    vector<InlineKeyboardButton::Ptr> row0;
    InlineKeyboardButton::Ptr accept(new InlineKeyboardButton);
    InlineKeyboardButton::Ptr reject(new InlineKeyboardButton);
    InlineKeyboardButton::Ptr forbid(new InlineKeyboardButton);

    accept->text = "\u2714 通过";
    accept->callbackData = "accept_" + to_string(new_post.hash);
    reject->text = "\u274c 拒绝";
    reject->callbackData = "reject_" + to_string(new_post.hash);
    forbid->text = "\360\237\232\253 封禁";
    forbid->callbackData = "forbid_" + to_string(new_post.hash);
    row0.push_back(accept);
    row0.push_back(reject);
    row0.push_back(forbid);
    decision_kbd->inlineKeyboard.push_back(row0);
    vector<std::string> opts;
    opts.push_back(json_escape(new_post.opt1));
    opts.push_back(json_escape(new_post.opt2));
    int32_t msgId = bot.getApi().sendPoll(admin_channel_id,
                                          json_escape("by " + new_post.username +
                                                      (new_post.assumption.length() > 0 ? "\n假设/前提：" +
                                                                                          new_post.assumption : "")),
                                          opts,
                                          false,
                                          0,
                                          decision_kbd)->messageId;
    bot.getApi().stopPoll(admin_channel_id, msgId, decision_kbd);
    return msgId;
}

void gMsgHandler(Bot &bot,
                 const Message::Ptr &message,
                 vector<pending_stage> &unfinished_polls,
                 unordered_map<uint32_t, pending_stage> &unpublished_polls) {
    if(message->poll) {
        if(message->poll->options.size() != 2) {
            bot.getApi().sendMessage(message->chat->id,
                                     "只能两个选项嗷~",
                                     false,
                                     message->messageId);
            return;
        }
        pending_stage new_poll;
        new_poll.stage = OPT2;
        if(message->forwardFrom)
            new_poll.username = message->forwardFrom->firstName + " " + message->forwardFrom->lastName;
        else if(message->forwardFromChat) {
            new_poll.username = message->forwardFromChat->title;
            if(message->forwardFromChat->firstName.length() > 0) {
                new_poll.username = new_poll.username + " (" + message->forwardFromChat->firstName;
                if(message->forwardFromChat->lastName.length() > 0)
                    new_poll.username = new_poll.username + " " + message->forwardFromChat->lastName;
                new_poll.username = new_poll.username + ")";
            }
        } else
            new_poll.username = message->from->firstName + " " + message->from->lastName;
        new_poll.user_id = message->from->id;
        new_poll.user_chatid = message->chat->id;
        if(message->poll->question.length() > 0) {
            new_poll.assumption = message->poll->question;
        }
        new_poll.opt1 = message->poll->options[0]->text;
        new_poll.opt2 = message->poll->options[1]->text;
        uint32_t h;
        do { h = get_hash(); }
        while(unpublished_polls.find(h) != unpublished_polls.end());
        new_poll.hash = h;
        new_poll.admin_channel_msgid = send_to_admin_channel(bot, new_poll);
        if(new_poll.admin_channel_msgid == 0)
            bot.getApi().sendMessage(message->chat->id, "投稿可能发生错误，请至休息室联系管理员确认。");
        else
            bot.getApi().sendMessage(message->chat->id, "投稿完成，待审核完成后将自动发送至频道。",
                                     false,
                                     message->messageId);
        unpublished_polls.insert(make_pair(h, new_poll));
    } else if(message->text.length() > 0) {
        for(auto i = unfinished_polls.begin(); i != unfinished_polls.end(); i++) {
            if(i->user_id == message->from->id && i->stage != OPT2) {
                if(message->text.length() > 0 && message->text.length() < 100) {
                    i->stage = (i->stage == NEW ? OPT1 : OPT2);
                    i->stage == OPT1 ? i->opt1 = message->text : i->opt2 = message->text;
                    bot.getApi().sendMessage(message->chat->id,
                                             i->stage == OPT1 ? "选项1提交成功。请发送选项2。"
                                                              : "投稿完成，待审核完成后将自动发送至频道。",
                                             false,
                                             message->messageId);
                } else {
                    bot.getApi().sendMessage(message->chat->id,
                                             "选项仅支持1~100字符的纯文本啊喂",
                                             false,
                                             message->messageId);
                }
                if(i->stage == OPT2) {
                    uint32_t h;
                    do { h = get_hash(); }
                    while(unpublished_polls.find(h) != unpublished_polls.end());
                    i->hash = h;
                    i->admin_channel_msgid = send_to_admin_channel(bot, *i);
                    if(i->admin_channel_msgid == 0) {
                        bot.getApi().sendMessage(message->chat->id, "投稿可能发生错误，请至休息室联系管理员确认。");
                    }
                    unpublished_polls.insert(make_pair(h, *i));
                    unfinished_polls.erase(i);
                }
                return;
            }
        }
    } else
        bot.getApi().sendMessage(message->chat->id,
                                 "你的没有正在进行的投稿，可以使用 /new 创建一个新投稿。",
                                 false,
                                 message->messageId);

}

int main(int argc, char *argv[]) {
    int watchdog_fd[2];
    pipe(watchdog_fd);
    pid_t child_id = fork();

    if(child_id < 0) {
        perror("fork");
        cerr << "Failed to create watchdog process, exiting...\n";
        exit(1);
    } else if(child_id) {
        // watchdog process
        close(watchdog_fd[1]);
        cout << "Watchdog PID:\t" << getpid() << "\nMaster PID:\t" << child_id << endl;
        time_t last_feed = time(nullptr);
        fcntl(watchdog_fd[0], F_SETFL, fcntl(watchdog_fd[0], F_GETFL) | O_NONBLOCK);
        while(time(nullptr) - last_feed < 40) {
            if(read(watchdog_fd[0], &last_feed, sizeof(time_t)) == sizeof(time_t))
                cout << "\033[1m[" << put_time(localtime(&last_feed), "%h %d %H:%M:%S") << "]\033[0m\t"
                     << "Yummy feed~\n";
            sleep(1);
        }
        close(watchdog_fd[0]);
        kill(child_id, SIGHUP);
        waitpid(child_id, NULL, WIFSIGNALED(0) | WIFEXITED(0));
        last_feed = time(nullptr);
        cout << "\033[1m[" << put_time(localtime(&last_feed), "%h %d %H:%M:%S") << "]\033[0m\t" << "Yummy bot~\n";
        exit(0);
    }
    close(watchdog_fd[0]);
    prctl(PR_SET_PDEATHSIG, SIGHUP);
    cout << "Master process started.\n";
    cout << "Author: Nicholas Wang <me@nicho1as.wang>" << endl
         << "Copyright (C) 2020  Licensed with GPLv3, for details, see: https://www.gnu.org/licenses/gpl-3.0.en.html"
         << endl << endl;
    if(argc > 1) {
        bot_token = argv[1];
        regex token_verify("\\d{9}:[0-9A-Za-z_-]{35}");
        cmatch m;
        int i = 0;
        do {
            if(regex_match(argv[1], m, token_verify)) {
                bot_token = string(argv[1]);
                break;
            } else {
                cout << "\033[1;31mErr: Invalid Bot token!\033[0m" << endl << "Please re-enter: ";
                cin >> argv[1];
            }
        } while(i++ < 3);
    }

    vector<pending_stage> unfinished_polls;
    unordered_map<uint32_t, pending_stage> unpublished_polls;
    unordered_map<int32_t, time_t> jail;
    jail_ptr = &jail;
    Bot bot(bot_token);
    bot.getEvents().onCommand("start", [&bot](const Message::Ptr &message) {
        if(message->date < birthTime)return;
        if(is_in_jail(message->from->id)) {
            bot.getApi().sendMessage(message->chat->id, "你当前处于被临时封禁状态。");
            return;
        }
        if(message->chat->type == Chat::Type::Private) {
            bot.getApi().sendMessage(message->chat->id, "使用说明：\n"
                                                        "/start\t显示本说明\n"
                                                        "/new\t开始新的投稿\n"
                                                        "/new assumption\t带有前提条件的投稿\n"
                                                        "/cancel\t取消当前投稿\n");
        }
    });

    bot.getEvents().onCommand("new", [&bot, &unfinished_polls](const Message::Ptr &message) {
        if(message->date < birthTime) return;
        if(is_in_jail(message->from->id)) {
            bot.getApi().sendMessage(message->chat->id, "你当前处于被临时封禁状态。");
            return;
        }
        for(pending_stage &i : unfinished_polls) {
            if(i.user_id == message->from->id && i.stage != OPT2) {
                bot.getApi().sendMessage(message->chat->id, "上一个创建的投稿似乎还未完成？");
                return;
            }
        }
        pending_stage new_poll;
        new_poll.stage = NEW;
        new_poll.username = message->from->firstName + " " + message->from->lastName;
        new_poll.user_id = message->from->id;
        new_poll.user_chatid = message->chat->id;
        if(message->text.length() > 5) {
            new_poll.assumption = message->text.substr(5);
        }
        unfinished_polls.push_back(new_poll);
        bot.getApi().sendMessage(message->chat->id, "请发送第一个选项");
    });

    bot.getEvents().onCommand("cancel", [&bot, &unfinished_polls](const Message::Ptr &message) {
        if(message->date < birthTime)return;
        if(is_in_jail(message->from->id)) {
            bot.getApi().sendMessage(message->chat->id, "你当前处于被临时封禁状态。");
            return;
        }
        for(auto i = unfinished_polls.begin(); i != unfinished_polls.end(); i++) {
            if(i->user_id == message->from->id && i->stage != OPT2) {
                unfinished_polls.erase(i);
                bot.getApi().sendMessage(message->chat->id, "取消成功？");
                return;
            }
        }
    });

    bot.getEvents().onCommand("admin", [&bot](const Message::Ptr &message) {
        if(message->date < birthTime)return;
        if(is_in_jail(message->from->id)) {
            bot.getApi().sendMessage(message->chat->id, "你当前处于被临时封禁状态。");
            return;
        }
        if(is_admin(bot, message->from->id))
            bot.getApi().sendMessage(message->chat->id,
                                     "由此加入[审核频道](" + bot.getApi().exportChatInviteLink(admin_channel_id) + ")",
                                     false,
                                     0,
                                     std::make_shared<GenericReply>(),
                                     "markdown");
        else bot.getApi().sendMessage(message->chat->id, "你不是频道管理员或没有频道消息发布权限。");
    });

    bot.getEvents().onNonCommandMessage([&bot, &unfinished_polls, &unpublished_polls](const Message::Ptr &message) {
        if(message->date < birthTime && message->chat->type != TgBot::Chat::Type::Private)return;
        if(is_in_jail(message->from->id)) {
            bot.getApi().sendMessage(message->chat->id, "你当前处于被临时封禁状态。");
            return;
        }
        gMsgHandler(bot, message, unfinished_polls, unpublished_polls);
    });

    bot.getEvents().onCallbackQuery([&bot, &unpublished_polls](const CallbackQuery::Ptr &query) {
        if((!is_admin(bot, query->from->id)) || query->message->chat->id != admin_channel_id) {
            bot.getApi().answerCallbackQuery(query->id, "艹谁把预印本泄漏了吗这是？强烈谴责！", true);
        }
        auto this_poll = unpublished_polls.find(stoi(query->data.substr(7)));
        if(this_poll == unpublished_polls.end()) {
            bot.getApi().answerCallbackQuery(query->id, "艹我怎么找不到这条投稿的！", true);
            return;
        }
        if(StringTools::startsWith(query->data, "accept")) {
            bot.getApi().editMessageReplyMarkup(admin_channel_id, this_poll->second.admin_channel_msgid, "",
                                                make_shared<GenericReply>());
            vector<std::string> opts;
            opts.push_back(json_escape(this_poll->second.opt1));
            opts.push_back(json_escape(this_poll->second.opt2));
            bot.getApi().sendPoll(channel_id,
                                  json_escape("by " + this_poll->second.username +
                                              (this_poll->second.assumption.length() > 0 ? "\n假设/前提：" +
                                                                                           this_poll->second.assumption
                                                                                         : "")),
                                  opts);
            //bot.getApi().sendMessage(this_poll->second.user_chatid,
            //"你的某条投稿已通过哦，快去 @WouldURather_CN 看看",
            //false,
            //0, std::make_shared<GenericReply>(),
            //"",
            //true);
            bot.getApi().answerCallbackQuery(query->id, "审核通过辣！", false);
            bot.getApi().sendMessage(admin_channel_id,
                                     "由 [" + query->from->firstName + " " + query->from->lastName + "](tg://user?id=" +
                                     to_string(query->from->id) + ") 通过。",
                                     false,
                                     this_poll->second.admin_channel_msgid,
                                     make_shared<GenericReply>(),
                                     "markdown",
                                     true);
            unpublished_polls.erase(this_poll);
        } else if(StringTools::startsWith(query->data, "reject")) {
            bot.getApi().sendMessage(this_poll->second.user_chatid, "你这条投稿不行啊，审核失败了", false,
                                     bot.getApi().forwardMessage(this_poll->second.user_chatid,
                                                                 admin_channel_id,
                                                                 this_poll->second.admin_channel_msgid,
                                                                 true)->messageId,
                                     make_shared<GenericReply>(), "", true);
            bot.getApi().editMessageReplyMarkup(admin_channel_id, this_poll->second.admin_channel_msgid, "",
                                                make_shared<GenericReply>());
            bot.getApi().sendMessage(admin_channel_id,
                                     "由 [" + query->from->firstName + " " + query->from->lastName + "](tg://user?id=" +
                                     to_string(query->from->id) + ") 拒绝。",
                                     false,
                                     this_poll->second.admin_channel_msgid,
                                     make_shared<GenericReply>(),
                                     "markdown",
                                     true);
            unpublished_polls.erase(this_poll);
            bot.getApi().answerCallbackQuery(query->id, "好，那我叫ta拿回去重写了！", false);
        } else if(StringTools::startsWith(query->data, "forbid")) {
            jail_ptr->insert(make_pair(this_poll->second.user_id, time(nullptr) + 3600));
            bot.getApi().sendMessage(this_poll->second.user_chatid, "你咋回事，咋叫人给封禁了！", false,
                                     bot.getApi().forwardMessage(this_poll->second.user_chatid,
                                                                 admin_channel_id,
                                                                 this_poll->second.admin_channel_msgid,
                                                                 true)->messageId,
                                     make_shared<GenericReply>(), "", true);
            bot.getApi().editMessageReplyMarkup(admin_channel_id, this_poll->second.admin_channel_msgid, "",
                                                make_shared<GenericReply>());
            bot.getApi().sendMessage(admin_channel_id,
                                     "由 [" + query->from->firstName + " " + query->from->lastName + "](tg://user?id=" +
                                     to_string(query->from->id) + ") 封禁。",
                                     false,
                                     this_poll->second.admin_channel_msgid,
                                     make_shared<GenericReply>(),
                                     "markdown",
                                     true);
            unpublished_polls.erase(this_poll);
        } else {
            bot.getApi().answerCallbackQuery(query->id, "艹你点了什么魔幻按钮啊！", true);
        }
    });

    signal(SIGINT, [](int s) {
        if(!sigintGot) {
            printf("SIGINT received, exiting...\n");
            sigintGot = true;
        }
    });


    cout << "Bot: " + string(bot.getApi().getMe()->username.c_str()) << endl
         << "Build: " << __DATE__ << " " << __TIME__ << endl
         << "----------" << endl
         << "\033[1m[" << put_time(localtime(&birthTime), "%h %d %H:%M:%S") << "]\033[0m\t" << "Birth." << endl;
    TgLongPoll longPoll(bot);
    while(!sigintGot) {
        time_t tm = time(nullptr);
        cout << "\033[1m[" << put_time(localtime(&tm), "%h %d %H:%M:%S") << "]\033[0m\t"
             << "longPoll started.\n";
        try { longPoll.start(); }
        catch(exception &e) {
            cout << "\033[1;31mErr\033[0m " << e.what() << endl;
        }
        tm = time(nullptr);
        cout << "\033[1m[" << put_time(localtime(&tm), "%h %d %H:%M:%S") << "]\033[0m\t" << "longPoll ended.\n";
        cin.ignore(0);
        write(watchdog_fd[1], &tm, sizeof(tm));
    }

    close(watchdog_fd[1]);
    return 0;
}

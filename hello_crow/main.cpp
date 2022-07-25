#include "crow_all.h"
#include <fstream>
#include <iostream>
#include <vector>
#include <cstdlib>
#include <unordered_set>
#include <mutex>
#include <boost/filesystem.hpp>

#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/oid.hpp>

#include <mongocxx/client.hpp>
#include <mongocxx/stdx.hpp>
#include <mongocxx/uri.hpp>
#include <mongocxx/instance.hpp>

using bsoncxx::builder::basic::kvp;
using bsoncxx::builder::basic::make_document;
using bsoncxx::builder::stream::close_array;
using bsoncxx::builder::stream::close_document;
using bsoncxx::builder::stream::document;
using bsoncxx::builder::stream::finalize;
using bsoncxx::builder::stream::open_array;
using bsoncxx::builder::stream::open_document;
using mongocxx::cursor;

void sendFile(crow::response &res, std::string fileName, std::string contentType)
{
    std::ifstream in("../public/" + fileName, std::ifstream::in);
    if (in)
    {
        std::ostringstream contents;
        contents << in.rdbuf();
        in.close();
        res.set_header("Content-Type", contentType);
        res.write(contents.str());
    }
    else
    {
        res.code = crow::status::NOT_FOUND;
    }

    res.end();
}

void sendHTML(crow::response &res, std::string fileName)
{
    sendFile(res, fileName, "text/html");
}

void sendImage(crow::response &res, std::string fileName)
{
    sendFile(res, fileName, "image/jpeg");
}

void sendScript(crow::response &res, std::string fileName)
{
    sendFile(res, fileName, "text/javascript");
}

void sendStyle(crow::response &res, std::string fileName)
{
    sendFile(res, fileName, "text/css");
}

void getView(crow::response &res, const std::string &filename, crow::mustache::context &x)
{
    res.set_header("Content-Type", "text/html");
    auto text = crow::mustache::load(filename + ".html").render_string(x);
    res.write(text);
    res.end();
}

void notFound(crow::response &res, const std::string &message)
{
    res.code = crow::status::NOT_FOUND;
    res.end();
}

int main(int argc, char *argv[])
{
    std::mutex mtx;
    std::unordered_set<crow::websocket::connection *> users;
    crow::SimpleApp app;
    crow::mustache::set_global_base("../public/");

    mongocxx::instance inst{};
    std::string mongoConnect = std::string(getenv("MONGODB_URI"));
    mongocxx::client conn{mongocxx::uri{mongoConnect}};
    auto collection = conn["cppweb"]["contacts"];

    CROW_ROUTE(app, "/ws")
        .websocket()
        .onopen([&](crow::websocket::connection &conn){
            std::lock_guard<std::mutex> _(mtx);
            users.insert(&conn);
        })
        .onclose([&](crow::websocket::connection &conn, const std::string &reason){
            std::lock_guard<std::mutex> _(mtx);
            users.erase(&conn);
        })
        .onmessage([&](crow::websocket::connection &/*conn*/, const std::string &data, bool isBinary){
            std::lock_guard<std::mutex> _(mtx);
            for(auto &user: users){
                if(isBinary){
                    user->send_binary(data);
                }else{
                    user->send_text(data);
                }
            }
        });

    CROW_ROUTE(app, "/add/<int>/<int>")
    ([](const crow::request &req, crow::response &res, const int &a, const int &b)
     {
        res.set_header("Content-Type","text/plain");

        std::ostringstream os;
        os << "Integer: " << a << "+" << b << "=" << a+b << "\n";
        res.write(os.str());
        res.end(); });

    CROW_ROUTE(app, "/add/<double>/<double>")
    ([](const crow::request &req, crow::response &res, const double &a, const double &b)
     {
        res.set_header("Content-Type","text/plain");

        std::ostringstream os;
        os << "Double: " << a << "+" << b << "=" << a+b << "\n";
        res.write(os.str());
        res.end(); });

    CROW_ROUTE(app, "/add/<string>/<string>")
    ([](const crow::request &req, crow::response &res, const std::string &a, const std::string &b)
     {
        res.set_header("Content-Type","text/plain");

        std::ostringstream os;
        os << "String: " << a << "+" << b << "=" << a+b << "\n";
        res.write(os.str());
        res.end(); });

    CROW_ROUTE(app, "/query")
    ([](const crow::request &req, crow::response &res)
     {
        auto firstName = req.url_params.get("firstname");
        auto lastName = req.url_params.get("lastname");

        std::ostringstream os;
        os << "Hello " << (firstName ? firstName : "") << " " << (lastName ? lastName : "") << "\n";

        res.write(os.str());
        res.end(); });

    CROW_ROUTE(app, "/api/v1/test").methods(crow::HTTPMethod::POST)([](const crow::request &req, crow::response &res)
                                                                    {
        std::string method = method_name(req.method);
        res.set_header("Content-type","text/plain");
        res.write(method + " rest_test");
        res.end(); });

    CROW_ROUTE(app, "/api/v1/contacts")
    ([&collection](const crow::request &req)
     {
        auto skip = req.url_params.get("skip");
        auto limit = req.url_params.get("limit");


        mongocxx::options::find opts;
        opts.skip(skip ? std::stoi(skip) : 0);
        opts.limit(limit ? std::stoi(limit) : 10);
        auto docs = collection.find({}, opts);
        std::vector<crow::json::rvalue> contacts;
        contacts.reserve(10);

        for(auto doc : docs){
            contacts.push_back(crow::json::load(bsoncxx::to_json(doc)));
        }

        crow::json::wvalue dto;
        dto["contacts"] = contacts;
        return crow::response{dto}; });

    CROW_ROUTE(app, "/contact/<string>")
    ([&collection](const crow::request &req, crow::response &res, std::string email)
     {
        auto doc = collection.find_one(make_document(kvp("email", email)));
        if(!doc) return notFound(res, "Contact");

        crow::json::wvalue dto;
        dto["contact"] = crow::json::load(bsoncxx::to_json(doc.value().view()));

        return getView(res, "contact", dto); });

    CROW_ROUTE(app, "/contact/<string>/<string>")
    ([&collection](const crow::request &req, crow::response &res, std::string firstName, std::string lastName)
     {
        auto doc = collection.find_one(make_document(kvp("firstName", firstName), kvp("lastName", lastName)));
        if(!doc)  return notFound(res, "Contact");

        crow::json::wvalue dto;
        dto["contact"] = crow::json::load(bsoncxx::to_json(doc.value().view()));

        return getView(res, "contact", dto); });

    CROW_ROUTE(app, "/contacts")
    ([&collection](const crow::request &req, crow::response &res)
     {
        mongocxx::options::find opts;
        opts.skip(9);
        opts.limit(10);
        auto docs = collection.find({}, opts);

        crow::json::wvalue dto;
        std::vector<crow::json::rvalue> contacts;
        contacts.reserve(10);

        for(auto doc : docs){
            contacts.push_back(crow::json::load(bsoncxx::to_json(doc)));
        }

        dto["contacts"] = contacts;

        return getView(res, "contacts", dto); });

    CROW_ROUTE(app, "/")
    ([](const crow::request &req, crow::response &res)
     { sendHTML(res, "index.html"); });

    CROW_ROUTE(app, "/styles/<string>")
    ([](const crow::request &req, crow::response &res, std::string filename)
     { sendStyle(res, "/styles/" + filename); });

    CROW_ROUTE(app, "/scripts/<string>")
    ([](const crow::request &req, crow::response &res, std::string filename)
     { sendScript(res, "/scripts/" + filename); });

    CROW_ROUTE(app, "/images/<string>")
    ([](const crow::request &req, crow::response &res, std::string filename)
     { sendImage(res, "/images/" + filename); });

    char *port = getenv("PORT");
    const uint16_t iPort = static_cast<uint16_t>(port != NULL ? std::stoi(port) : 18080);
    std::cout << "PORT = " << iPort << "\n";

    app.port(iPort).multithreaded().run();
}
#pragma once

#include "concept/rpc.hxx"
#include "glaze/json/write.hpp"
#include "util/logger.hxx"

struct PayloadType {
  uint32_t id;
  std::string message;
};

struct EchoRpc : RpcBase<"Echo", PayloadType, PayloadType> {
  Response operator()(const Request& req) {
    // INFO("{}", glz::write_json<>(req).value_or("Corrupted Payload!"));
    // req.id++;
    // req.message += ", World";
    return req;
  };
};

struct HelloRpc : RpcBase<"Hello", std::string, std::string> {
  Response operator()(const Request& req) {
    INFO("{}", req);
    return "Echo: " + req;
  };
};

static_assert(Rpc<EchoRpc>, "?");
static_assert(Rpc<HelloRpc>, "?");

const PayloadType payload_4k = {
    .id = 666,
    .message =
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit. In condimentum nisi lorem, ut congue metus efficitur "
        "id. Nunc id quam sed lectus bibendum sodales. Aenean in facilisis turpis, vel tempor velit. Aliquam semper "
        "dolor eu aliquet cursus. Nulla sodales nisi purus, at laoreet erat tincidunt at. Proin convallis purus a "
        "condimentum ornare. Donec suscipit mauris sed vulputate aliquet. Phasellus gravida justo urna, id congue "
        "lacus dapibus quis. Maecenas risus nisl, vehicula a molestie eget, varius ut quam. Vestibulum quis blandit "
        "metus, at fringilla magna. Morbi finibus dui lacus, viverra faucibus dolor laoreet ac. Praesent vehicula "
        "neque tempor lacus scelerisque, id tincidunt nulla condimentum. Etiam convallis lobortis nulla ut fringilla. "
        "Phasellus aliquet euismod mauris ac commodo. Nam dictum, nunc sed tempus dictum, sapien tortor commodo "
        "tortor, ac fermentum lacus metus nec ex. Nunc ac finibus est. Integer ut vestibulum ante. Sed eu velit nec mi "
        "consectetur suscipit eget non diam. Integer convallis elit ut magna pellentesque, ut egestas dolor mollis. In "
        "convallis laoreet sem id venenatis. Integer dictum mattis neque, sit amet sollicitudin mauris finibus ornare. "
        "Donec finibus ipsum at dapibus pharetra. Vestibulum scelerisque vulputate odio. Maecenas at fermentum erat. "
        "Aliquam erat volutpat. Donec bibendum consectetur pulvinar. Vestibulum bibendum tempus quam, id lacinia ex "
        "interdum ac. Nullam sed diam elementum, imperdiet augue sed, rhoncus lectus. Nulla vulputate metus ut sem "
        "scelerisque, et consectetur tellus suscipit. Quisque non tincidunt elit. Aenean laoreet libero at urna "
        "aliquet bibendum. Aenean sodales orci nibh, eu imperdiet velit varius id. Suspendisse imperdiet gravida sem "
        "vel consequat. Aenean ut fringilla magna. Cras nec lectus at dolor pretium congue nec laoreet turpis. Nam sit "
        "amet nunc in tellus pellentesque fringilla. Nullam placerat massa vel magna facilisis mollis. Vivamus sapien "
        "orci, pretium vitae velit a, aliquet pellentesque nibh. Curabitur posuere eleifend quam, in pellentesque "
        "lectus lacinia eu. Aliquam erat volutpat. Quisque a ultrices dui, in interdum lacus. Phasellus nec arcu ac "
        "lorem vulputate accumsan. Quisque arcu lorem, auctor ut mollis quis, dictum sit amet elit. Nam accumsan dolor "
        "et mi semper gravida. Vivamus quis cursus arcu. Morbi viverra aliquam nisl, quis egestas lectus sagittis in. "
        "Integer vitae dui odio. Duis blandit ante at facilisis sodales. Nulla non pretium nunc, eu placerat arcu. "
        "Quisque semper eleifend tellus vel dignissim. Duis vel ipsum leo. Fusce consectetur lectus ac est eleifend "
        "dignissim. Praesent rhoncus lacinia vehicula. Donec placerat urna ut tortor bibendum luctus. Phasellus "
        "euismod pretium diam, at dapibus nibh. Pellentesque sem justo, consequat non tortor quis, pulvinar vehicula "
        "mauris. Nam aliquam interdum mauris at luctus. Curabitur turpis dui, semper non luctus tristique, scelerisque "
        "sit amet metus. Vivamus non nulla dignissim, efficitur odio non, fermentum libero. Nullam bibendum ac ex at "
        "fringilla. Vivamus sollicitudin, arcu vitae condimentum placerat, felis libero venenatis purus, id dapibus "
        "neque eros lobortis massa. Interdum et malesuada fames ac ante ipsum primis in faucibus. Cras eros purus, "
        "luctus viverra ante non, vulputate facilisis magna. Quisque condimentum neque ut enim fringilla venenatis. "
        "Nunc in ipsum rutrum ex congue sagittis. Fusce facilisis magna orci, eget commodo massa auctor eget. Nulla "
        "porta nibh vitae sapien porta, non placerat lectus molestie. Etiam suscipit justo nec arcu rutrum, ac "
        "tincidunt velit consequat. Morbi rhoncus dolor non lacus volutpat, imperdiet auctor diam tempor. Aenean "
        "convallis, felis et molestie aliquam, nisl orci mattis arcu, in mollis nibh arcu ac elit. Vestibulum "
        "vehicula, orci sit amet tempor dapibus, lorem nunc pharetra mi, sit amet vehicula massa nisi sed mi.Donec sed "
        "leo sit amet nibh elementum suscipit. Integer vitae ornare felis. Vestibulum semper quam metus, vitae aliquet "
        "diam lacinia eue."};

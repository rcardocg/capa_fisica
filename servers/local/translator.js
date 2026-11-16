(function (global) {
  "use strict";

  function codeToBinary(code) {
    var binary = code.toString(2);
    while (binary.length < 8) {
      binary = "0" + binary;
    }
    return binary;
  }

  function translate(text) {
    if (typeof text !== "string" || text.length === 0) {
      return { error: "Escribe un mensaje." };
    }

    var characters = Array.from(text);
    var entries = [];
    var binaryParts = [];
    var index;

    for (index = 0; index < characters.length; index++) {
      var character = characters[index];
      var code = character.codePointAt(0);

      if (code > 255) {
        return {
          error: 'El carácter "' + character + '" no se puede representar en 8 bits (código ' + code + ")."
        };
      }

      entries.push({ character: character, code: code });
      binaryParts.push(codeToBinary(code));
    }

    return {
      entries: entries,
      asciiText: entries.map(function (entry) {
        return entry.character + ":" + entry.code;
      }).join("  "),
      binaryGroups: binaryParts.join(" "),
      binary: binaryParts.join(""),
      bitLength: binaryParts.length * 8
    };
  }

  global.Translator = {
    translate: translate,
    codeToBinary: codeToBinary
  };
})(typeof window !== "undefined" ? window : this);

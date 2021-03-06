// -*- mode: js; js-indent-level: 4; indent-tabs-mode: nil -*-

const DocInfo = imports.misc.docInfo;
const Lang = imports.lang;
const Params = imports.misc.params;
const Search = imports.ui.search;

const DocSearchProvider = new Lang.Class({
    Name: 'DocSearchProvider',
    Extends: Search.SearchProvider,

    _init: function(name) {
        this.parent(_("RECENT ITEMS"));
        this._docManager = DocInfo.getDocManager();
    },

    getResultMeta: function(resultId) {
        let docInfo = this._docManager.lookupByUri(resultId);
        if (!docInfo)
            return null;
        return { 'id': resultId,
                 'name': docInfo.name,
                 'createIcon': function(size) {
                                   return docInfo.createIcon(size);
                               }
               };
    },

    activateResult: function(id, params) {
        params = Params.parse(params, { workspace: -1,
                                        timestamp: 0 });

        let docInfo = this._docManager.lookupByUri(id);
        docInfo.launch(params.workspace);
    },

    getInitialResultSet: function(terms) {
        return this._docManager.initialSearch(terms);
    },

    getSubsearchResultSet: function(previousResults, terms) {
        return this._docManager.subsearch(previousResults, terms);
    }
});

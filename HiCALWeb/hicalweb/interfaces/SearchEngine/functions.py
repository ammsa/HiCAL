from hicalweb.interfaces.DocumentSnippetEngine import functions as DocEngine
from collections import OrderedDict
import environ
import re
import string
# import jnius_config
#
# env = environ.Env()
# ANSERINI_JAR = env.str('ANSERINI_JAR')
# ANSERINI_INDEX = env.str('ANSERINI_INDEX')
#
#
# jnius_config.set_classpath(ANSERINI_JAR)
#
# print(ANSERINI_JAR, ANSERINI_INDEX)
#
#
# from jnius import autoclass
#
try:
    JString = autoclass('java.lang.String')
    JSearcher = autoclass('io.anserini.search.SimpleSearcher')

    searcher = JSearcher(JString(ANSERINI_INDEX))
except Exception as e:
    print("Ops! error occured while configuring search. {}".format(e))

import re
def striphtml(data):
    p = re.compile(r'<.*?>')
    return p.sub('', data)

def highlight_terms(query, content):
    terms = query.lower().split(" ")

    for word in content.split(" "):
        word = re.sub('[%s]' % re.escape(string.punctuation), '', word)
        if len(word) <= 2: continue
        if word.lower() in terms:
            content = content.replace(word, "<b>{}</b>".format(word))
    return content


def get_documents(query, start=0, numdisplay=20):
    doc_ids = []
    result = OrderedDict()

    hits = searcher.search(JString(query))
    for docnum, doc in enumerate(hits):
        title = DocEngine.get_document_title(doc.docid)
        title = highlight_terms(query, str(title))

        try:
            snippet = striphtml(doc.content)[:250]
            snippet = highlight_terms(query, str(snippet))
        except Exception:
            snippet = 'Unknown error from Anserini'
        parsed_doc = {
           "rank": docnum,
           "docno": hits[docnum].docid,
           "title": title,
           "snippet": snippet
        }

        result[hits[docnum].docid] = parsed_doc
        doc_ids.append(hits[docnum].docid)

    return result, doc_ids, ""

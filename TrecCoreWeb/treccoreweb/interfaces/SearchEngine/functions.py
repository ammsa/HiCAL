from collections import OrderedDict

import httplib2
import urllib.parse
import xmltodict

from config.settings.base import SEARCH_SERVER_IP, SEARCH_SERVER_PORT


def get_documents(query, start=0, numdisplay=10):
    h = httplib2.Http()
    url = "http://{}:{}/treccore/websearchapi/search.php?{}"

    parameters = {'start': start, 'numdisplay': numdisplay, 'query': query}
    parameters = urllib.parse.urlencode(parameters)
    resp, content = h.request(url.format(SEARCH_SERVER_IP,
                                         SEARCH_SERVER_PORT,
                                         parameters),
                              method="GET")

    if resp and resp.get("status") == "200":
        xmlDict = xmltodict.parse(content)
        try:
            xmlResult = xmlDict['search-response']['results']['result']
        except TypeError:
            return None, None

        doc_ids = []
        result = OrderedDict()
        for doc in xmlResult:
            parsed_doc = {
                "rank": doc["rank"],
                "docno": doc["docno"],
                "title": doc["title"],
                "snippet": doc["snippet"]
            }
            result[doc["docno"]] = parsed_doc
            doc_ids.append(doc["docno"])

        return result, doc_ids

    return None, None

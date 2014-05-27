var doc = document; var links = document.links;

//Calculate if an element is visible
function isVisible(el) {
    if (el == doc) {
        return true;
    }
    if (!el) {
        return false;
    }
    if (!el.parentNode) {
        return false;
    }
    if (el.style) {
        if (el.style.display == "none") {
            return false;
        }
        if (el.style.visibility == "hidden") {
            return false;
        }
    }
    return isVisible(el.parentNode);
}
function getTitle(li, type) {
  var nodes = li.childNodes;
  var text = "";
  for (i=0; i<nodes.length; i++) {
    if (nodes[i].nodeType==3) {
      if (nodes[i].data!="") {
        text = nodes[i].data;
        i=nodes.length;
        type=0;
      }
    } else {
      if (typeof nodes=='function' && nodes(i).alt!="" && type>1) {
        text=nodes(i).alt;
        type=1;
      } else if (typeof nodes=='function' && nodes(i).title!="" && type>2) {
        text=nodes(i).title;
        type=2;
      } else if (typeof nodes=='function' && nodes(i).id!="" && type>3) {
        text=nodes(i).id;
        type=3;
      } else if (typeof nodes=='function' && nodes(i).src!="" && type>4) {
        text=nodes(i).id;
        type=4;
      } else {
        title = getTitle(nodes[i]);
        if (title[0]!="" && title[1]<type) {
          text = title[0];
          type = title[1];
        }
      }
    }
  }
  return [text, type];
}

//Returns a list of all links
function addLinks() {
    res = "";
    urls = new Array();
    titles = new Array();
    for (var l = 0; l < links.length; l++) {
        var li = links[l];
        if (isVisible(li)) {
            title = getTitle(li, 5)[0];
            if (title=="") title = "empty";
            url = li.href;
            if (!urls[url]) {
              urls[url] = true;
              if (!titles[title]) {
                res += url + "+++" + title + "\n";
                titles[title]=1;
              } else {
                res += url + "+++" + title + " " + titles[title] + "\n";
                titles[title]++;
              }
            }
        }
    }
    return res;
}
addLinks();
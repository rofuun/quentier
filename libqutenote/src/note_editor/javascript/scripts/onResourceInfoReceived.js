function onResourceInfoReceived(resourceHash, filePath, displayName, displaySize) {
    console.log("onResourceInfoReceived: hash = " + resourceHash +
                ", file path = " + filePath + ", name = " + displayName +
                ", size = " + displaySize);

    var resources = document.querySelectorAll("[hash=\"" + resourceHash + "\"]");
    if (!resources) {
        return;
    }

    var escapedPath = filePath.replace(/[\\"']/g, '\\$&').replace(/\u0000/g, '\\0');

    var resource = resources[0];
    if (!resource) {
        return;
    }

    resource.setAttribute("src", escapedPath);

    var resourceName = resource.getElementsByClassName("resource-name");
    if (resourceName && resourceName[0]) {
        resourceName[0].textContent = displayName;
        console.log("Set resource display name to " + displayName);
    }
    else {
        console.log("Can't find child element for resource display name");
    }

    var resourceSize = resource.getElementsByClassName("resource-size");
    if (resourceSize && resourceSize[0]) {
        resourceSize[0].textContent = displaySize;
        console.log("Set resource display size to " + displaySize);
    }
    else {
        console.log("Can't find child element for resource display size");
    }
}
